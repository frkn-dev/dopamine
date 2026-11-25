#include "core/peerConfigBuilder.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace
{
// Canonical spelling of every obfuscation key we understand, keyed by the
// normalized form (lower case, no underscores) so both the api's
// `random_trailers` and an already-canonical `RandomTrailers` resolve.
const QHash<QString, QString> &obfuscationKeys()
{
    static const QHash<QString, QString> keys = {
        { "jc", "Jc" }, { "jmin", "Jmin" }, { "jmax", "Jmax" },
        { "s1", "S1" }, { "s2", "S2" }, { "s3", "S3" }, { "s4", "S4" },
        { "h1", "H1" }, { "h2", "H2" }, { "h3", "H3" }, { "h4", "H4" },
        { "i1", "I1" }, { "i2", "I2" }, { "i3", "I3" }, { "i4", "I4" }, { "i5", "I5" },
        { "randomtrailers", "RandomTrailers" },
        { "disablecookies", "DisableCookies" },
        { "headerprotectionkey", "HeaderProtectionKey" },
        { "contentpaddingaddition", "ContentPaddingAddition" },
        { "rekeyaftertime", "RekeyAfterTime" },
        { "rekeytimeout", "RekeyTimeout" },
        { "rejectaftertime", "RejectAfterTime" },
        { "keepalivetimeout", "KeepaliveTimeout" },
        { "maxhandshakeattempts", "MaxHandshakeAttempts" },
    };
    return keys;
}

// Every consumer reads the profile as a string (the INI, the daemon's
// InterfaceConfig, the iOS WGConfig), so numbers and the AWG 3.1 on/off flags
// are flattened the same way. Arrays and objects are not part of a profile.
QString scalarValue(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Double:
        return QString::number(static_cast<qint64>(value.toDouble()));
    case QJsonValue::String:
        return value.toString().trimmed();
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("on") : QStringLiteral("off");
    default:
        return {};
    }
}
} // namespace

namespace amnezia::peers
{

QString PeerConfigBuilder::canonicalObfuscationKey(const QString &apiKey)
{
    QString normalized = apiKey.trimmed().toLower();
    normalized.remove(QLatin1Char('_'));
    return obfuscationKeys().value(normalized);
}

QString PeerConfigBuilder::obfuscationBlock(const PoolNode &node)
{
    if (!node.hasObfuscation || node.obfuscation.isEmpty()) {
        return {};
    }

    QStringList lines;
    for (auto it = node.obfuscation.constBegin(); it != node.obfuscation.constEnd(); ++it) {
        const auto key = canonicalObfuscationKey(it.key());
        if (key.isEmpty()) {
            continue;
        }
        const auto value = scalarValue(it.value());
        if (!value.isEmpty()) {
            lines.append(QStringLiteral("%1 = %2").arg(key, value));
        }
    }
    return lines.join('\n');
}

QJsonObject PeerConfigBuilder::obfuscationData(const PoolNode &node)
{
    QJsonObject out;
    if (!node.hasObfuscation) {
        return out;
    }
    for (auto it = node.obfuscation.constBegin(); it != node.obfuscation.constEnd(); ++it) {
        const auto key = canonicalObfuscationKey(it.key());
        if (key.isEmpty()) {
            continue;
        }
        const auto value = scalarValue(it.value());
        if (!value.isEmpty()) {
            out.insert(key, value);
        }
    }
    return out;
}

QString PeerConfigBuilder::build(const PoolCard &card, const PoolNode &node, const QString &privateKey, const QString &address)
{
    const bool isAmnezia = card.proto.startsWith(QStringLiteral("amnezia"));
    const auto dns = node.dns.isEmpty() ? QStringLiteral("1.1.1.1, 1.0.0.1") : node.dns.join(QStringLiteral(", "));
    const auto peerAddress = address.contains('/') ? address : address + QStringLiteral("/32");
    const auto keepalive = node.keepalive > 0 ? node.keepalive : 25;

    QStringList lines;
    lines << QStringLiteral("[Interface]");
    lines << QStringLiteral("PrivateKey = %1").arg(privateKey);
    lines << QStringLiteral("Address = %1").arg(peerAddress);
    lines << QStringLiteral("DNS = %1").arg(dns);
    lines << QStringLiteral("MTU = %1").arg(node.mtu);

    if (isAmnezia) {
        const auto obfuscation = obfuscationBlock(node);
        if (!obfuscation.isEmpty()) {
            lines << obfuscation;
        }
    }

    lines << QString();
    lines << QStringLiteral("[Peer]");
    lines << QStringLiteral("PublicKey = %1").arg(node.serverPublicKey);
    // IPv4-only full tunnel: our nodes provide no IPv6, and advertising ::/0
    // blackholes IPv6-preferring apps (the same reason the v1 AWG import keeps
    // AllowedIPs to 0.0.0.0/0).
    lines << QStringLiteral("AllowedIPs = 0.0.0.0/0");
    lines << QStringLiteral("Endpoint = %1").arg(node.endpoint);
    lines << QStringLiteral("PersistentKeepalive = %1").arg(keepalive);

    return lines.join('\n') + '\n';
}

namespace
{
// Host part of `host:port`. IPv4 and hostnames only (our nodes), same spelling
// the peers importer uses.
QString endpointHostOf(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    return colon > 0 ? endpoint.left(colon).trimmed() : endpoint.trimmed();
}

int endpointPortOf(const QString &endpoint)
{
    const int colon = endpoint.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0 || colon >= endpoint.size() - 1) {
        return 0;
    }
    bool ok = false;
    const int port = endpoint.mid(colon + 1).toInt(&ok);
    return ok ? port : 0;
}

// `vless_xhttp_cdn` card → xray outbound.
//
// The api hands out the dial parameters flat (…, `uuid`, `transport`, `path`,
// `host`, `sni`, `mode`, `alpn`, `padding`), and the node dialled is the card's
// own endpoint — the v1 CDN form fronted the same inbound by name, a v2 card
// states the entry IP and the announced name separately. The outbound is the
// standard xray schema the app already runs for v1 VLESS (`network: "xhttp"`,
// `security: "tls"`), so nothing here is a second transport implementation.
//
// Missing `uuid`, `host` or `path` = not dialable: the node cannot authenticate
// the user or route the request, and inventing a value would only fail after
// the tunnel comes up. `padding` travels only when the api sent all three of
// `header`/`key`/`bytes` — a partial block is a server bug, not a default.
QString buildVlessClientConfig(const PoolCard &card, const PoolNode &node)
{
    const QJsonObject payload = node.payload;
    const QString uuid = payload.value(QStringLiteral("uuid")).toString().trimmed();
    const QString path = payload.value(QStringLiteral("path")).toString().trimmed();
    const QString host = payload.value(QStringLiteral("host")).toString().trimmed();
    const QString transport = payload.value(QStringLiteral("transport")).toString().trimmed().toLower();
    if (uuid.isEmpty() || path.isEmpty() || host.isEmpty()) {
        qWarning() << "[PEERS] vless client_config" << card.cardId << "is not dialable (payload keys:"
                   << payload.keys() << ") — uuid/host/path are required";
        return {};
    }
    if (!transport.isEmpty() && transport != QStringLiteral("xhttp")) {
        qWarning() << "[PEERS] vless client_config" << card.cardId << "announces transport" << transport
                   << "— only xhttp is supported by this build";
        return {};
    }

    const QString dialHost = payload.value(QStringLiteral("server")).toString().trimmed().isEmpty()
            ? endpointHostOf(node.endpoint)
            : payload.value(QStringLiteral("server")).toString().trimmed();
    const int payloadPort = payload.value(QStringLiteral("port")).toInt();
    const int dialPort = payloadPort > 0 ? payloadPort : endpointPortOf(node.endpoint);
    if (dialHost.isEmpty() || dialPort <= 0) {
        qWarning() << "[PEERS] vless client_config" << card.cardId << "has no dial address:"
                   << node.endpoint;
        return {};
    }

    QJsonObject user;
    user[QStringLiteral("id")] = uuid;
    user[QStringLiteral("encryption")] = QStringLiteral("none");
    const QString flow = payload.value(QStringLiteral("flow")).toString().trimmed();
    if (!flow.isEmpty()) {
        user[QStringLiteral("flow")] = flow;
    }

    QJsonObject vnext;
    vnext[QStringLiteral("address")] = dialHost;
    vnext[QStringLiteral("port")] = dialPort;
    vnext[QStringLiteral("users")] = QJsonArray { user };

    QJsonObject settings;
    settings[QStringLiteral("vnext")] = QJsonArray { vnext };

    // xhttp transport. `host` is the Host header the inbound/CDN expects,
    // `path` the request path and `mode` the packet-up/auto split; all three
    // come from the card, none is defaulted.
    QJsonObject xhttp;
    xhttp[QStringLiteral("path")] = path;
    xhttp[QStringLiteral("host")] = host;
    const QString mode = payload.value(QStringLiteral("mode")).toString().trimmed();
    if (!mode.isEmpty()) {
        xhttp[QStringLiteral("mode")] = mode;
    }
    const QJsonObject padding = payload.value(QStringLiteral("padding")).toObject();
    if (padding.value(QStringLiteral("header")).isString()
        && padding.value(QStringLiteral("key")).isString()
        && padding.value(QStringLiteral("bytes")).isString()) {
        QJsonObject extra;
        extra[QStringLiteral("padding")] = padding;
        xhttp[QStringLiteral("extra")] = extra;
    } else if (!padding.isEmpty()) {
        qWarning() << "[PEERS] vless client_config" << card.cardId
                   << "sends an incomplete padding block — ignored";
    }

    // TLS: the CDN/announced name the certificate is issued for, never the dial
    // address when the payload names one.
    const QString sni = payload.value(QStringLiteral("sni")).toString().trimmed();
    QJsonObject tls;
    tls[QStringLiteral("serverName")] = sni.isEmpty() ? host : sni;
    const QJsonArray alpn = payload.value(QStringLiteral("alpn")).toArray();
    if (!alpn.isEmpty()) {
        tls[QStringLiteral("alpn")] = alpn;
    }

    QJsonObject stream;
    stream[QStringLiteral("network")] = QStringLiteral("xhttp");
    stream[QStringLiteral("security")] = QStringLiteral("tls");
    stream[QStringLiteral("tlsSettings")] = tls;
    stream[QStringLiteral("xhttpSettings")] = xhttp;

    QJsonObject outbound;
    outbound[QStringLiteral("protocol")] = QStringLiteral("vless");
    outbound[QStringLiteral("settings")] = settings;
    outbound[QStringLiteral("streamSettings")] = stream;

    qDebug().noquote() << QStringLiteral("[PEERS] vless client_config %1 -> xhttp %2%3 (sni %4)")
                                  .arg(card.cardId, dialHost, path,
                                       tls.value(QStringLiteral("serverName")).toString());

    QJsonObject root;
    root[QStringLiteral("outbounds")] = QJsonArray { outbound };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
} // namespace

// Client-built tunnel of a `client_config` card.
//
// The api hands out the dial parameters per node (`payload`) instead of an
// address: a hysteria2 node terminates the tunnel itself, so nothing on the
// client side has to be registered on the shard. The outbound is emitted in the
// schema the v1 plane already ships and the app already runs — the legacy one —
// so the single conversion that exists
// (`apiUtils::translateLegacyHysteria2Outbound`, applied by
// VpnConfigurationsController for every platform and again, idempotently, by
// IosController::setupXray) is the only hysteria2 code in the client.
QString PeerConfigBuilder::buildClientConfig(const PoolCard &card, const PoolNode &node)
{
    const QString proto = card.proto.trimmed().toLower();
    if (proto.startsWith(QStringLiteral("vless"))) {
        return buildVlessClientConfig(card, node);
    }
    if (!proto.startsWith(QStringLiteral("hysteria"))) {
        // No client builder for any other client_config protocol: guessing a
        // config here would produce one nobody validated.
        qWarning() << "[PEERS] client_config card" << card.cardId << "of protocol" << proto
                   << "has no client builder in this build";
        return {};
    }

    const QJsonObject payload = node.payload;
    const QString auth = payload.value(QStringLiteral("auth")).toString().trimmed();
    const QString server = payload.value(QStringLiteral("server")).toString().trimmed();
    const int port = payload.value(QStringLiteral("port")).toInt();
    if (auth.isEmpty() || server.isEmpty() || port <= 0) {
        // auth is the subscription's own token (the same one the v1 plane
        // hands out and the node's auth service validates); an invented
        // password or a guessed endpoint would only fail at connect time.
        qWarning() << "[PEERS] hysteria2 payload of node" << node.endpoint
                   << "is not dialable, keys:" << payload.keys();
        return {};
    }

    const QString payloadSni = payload.value(QStringLiteral("sni")).toString().trimmed();
    const QString sni = payloadSni.isEmpty() ? server : payloadSni;

    QJsonObject serverEntry;
    serverEntry[QStringLiteral("address")] = server;
    serverEntry[QStringLiteral("port")] = port;
    // The legacy schema spells the credential "password"; the api calls it
    // "auth". Same value, and it is what translateLegacyHysteria2Outbound reads.
    serverEntry[QStringLiteral("password")] = auth;
    serverEntry[QStringLiteral("serverName")] = sni;

    QJsonObject settings;
    settings[QStringLiteral("servers")] = QJsonArray { serverEntry };
    // hysteria2 obfs. The api hands the PSK flat (`{type, password}`); hysteria2
    // itself only accepts its own nested shape
    // (`obfs: {type: salamander, salamander: {password}}`) — a flat
    // `obfs.password` is read as an empty PSK and rejected with "PSK must be at
    // least 4 bytes". Emit the nested form here; the transport translation
    // (translateLegacyHysteria2Outbound) turns it into the UDP mask the core
    // applies. No obfs, or an empty password, means plain hysteria2 and the
    // block is left out entirely.
    const QJsonObject payloadObfs = payload.value(QStringLiteral("obfs")).toObject();
    const QString obfsType = payloadObfs.value(QStringLiteral("type")).toString().trimmed();
    const QString obfsPassword = payloadObfs.value(QStringLiteral("password")).toString();
    if (!obfsType.isEmpty() && !obfsPassword.isEmpty()) {
        QJsonObject obfs;
        obfs[QStringLiteral("type")] = obfsType;
        obfs[obfsType] = QJsonObject { { QStringLiteral("password"), obfsPassword } };
        settings[QStringLiteral("obfs")] = obfs;
    }

    QJsonObject tlsSettings;
    tlsSettings[QStringLiteral("serverName")] = sni;
    tlsSettings[QStringLiteral("fingerprint")] = QStringLiteral("chrome");
    // hysteria2 runs over HTTP/3; the translator pins this too.
    tlsSettings[QStringLiteral("alpn")] = QJsonArray { QStringLiteral("h3") };
    tlsSettings[QStringLiteral("allowInsecure")] = payload.value(QStringLiteral("insecure")).toBool(false);

    QJsonObject streamSettings;
    streamSettings[QStringLiteral("network")] = QStringLiteral("udp");
    streamSettings[QStringLiteral("security")] = QStringLiteral("tls");
    streamSettings[QStringLiteral("tlsSettings")] = tlsSettings;

    QJsonObject outbound;
    outbound[QStringLiteral("protocol")] = QStringLiteral("hysteria2");
    outbound[QStringLiteral("settings")] = settings;
    outbound[QStringLiteral("streamSettings")] = streamSettings;

    QJsonObject root;
    root[QStringLiteral("outbounds")] = QJsonArray { outbound };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

namespace
{
// SHA-256 of the payload array in node order. QJsonObject sorts its keys, so
// two identical payloads hash the same whatever order the api serialized them
// in, and any changed field (obfs, auth, sni, port) hashes differently.
QString fingerprintOfPayloads(const QJsonArray &payloads)
{
    if (payloads.isEmpty()) {
        return {};
    }
    const QByteArray compact = QJsonDocument(payloads).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(compact, QCryptographicHash::Sha256).toHex());
}
} // namespace

QString payloadFingerprint(const QVector<PoolNode> &nodes)
{
    QJsonArray payloads;
    for (const auto &node : nodes) {
        // Only what the config is built from: a card's endpoints and labels may
        // move without invalidating a tunnel, its dial parameters may not.
        if (!node.payload.isEmpty()) {
            payloads.append(node.payload);
        }
    }
    return fingerprintOfPayloads(payloads);
}

QString payloadFingerprint(const QJsonArray &nodeJson)
{
    QJsonArray payloads;
    for (const QJsonValue &value : nodeJson) {
        const QJsonObject payload = value.toObject().value(QStringLiteral("payload")).toObject();
        if (!payload.isEmpty()) {
            payloads.append(payload);
        }
    }
    return fingerprintOfPayloads(payloads);
}

} // namespace amnezia::peers
