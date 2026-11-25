#pragma once

// The ONE node endpoint shape, shared by every level of the contract:
//
//   [[node.endpoints]]        (node config, ffcore)
//   "endpoints": [{...}]      (node -> api announcement, api -> client payload)
//   node_ips/node_ports/      (legacy payload — kept for old clients)
//   node_weights
//
// One record: { "ip": "82.148.231.161", "port": 443, "weight": 200, "label": "nl-01" }.
// `port` absent = the protocol/inbound port, `weight` absent = 100, `label` is
// for logs only and is never read here.
//
// The rest of the client (address picker, health cache, port/weight lookups)
// speaks the legacy triple; this header lets a caller prefer `endpoints` when
// the payload carries it and fall back to the old fields otherwise, so both
// sources produce exactly the same behaviour.
//
// Header-only and QtCore-only on purpose: it is compiled into the app and into
// the standalone test (tests/node_endpoints_test.cpp).

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace NodeEndpoints {

constexpr int kDefaultWeight = 100;

// `endpoints[]` as sent on the wire; an empty array when the payload is an old
// one (no `endpoints` key) or the key is not an array.
inline QJsonArray entries(const QJsonObject &serverConfig)
{
    const QJsonValue raw = serverConfig.value(QStringLiteral("endpoints"));
    return raw.isArray() ? raw.toArray() : QJsonArray();
}

// True when this payload uses the new shape (a non-empty `endpoints[]`).
inline bool present(const QJsonObject &serverConfig)
{
    return !entries(serverConfig).isEmpty();
}

// Entry addresses in payload order: `endpoints[].ip` when present, else the
// legacy `node_ips` array. Duplicates and blank values are dropped; an empty
// result means "this row is reached by its hostName".
inline QStringList ips(const QJsonObject &serverConfig)
{
    QStringList out;
    if (present(serverConfig)) {
        for (const QJsonValue &value : entries(serverConfig)) {
            const QString ip = value.toObject().value(QStringLiteral("ip")).toString().trimmed();
            if (!ip.isEmpty() && !out.contains(ip)) {
                out.append(ip);
            }
        }
        return out;
    }
    for (const QJsonValue &value : serverConfig.value(QStringLiteral("node_ips")).toArray()) {
        const QString ip = value.toString().trimmed();
        if (!ip.isEmpty() && !out.contains(ip)) {
            out.append(ip);
        }
    }
    return out;
}

// The endpoint record of one address, or an empty object when the payload has
// no matching entry.
inline QJsonObject entryFor(const QJsonObject &serverConfig, const QString &ip)
{
    if (!present(serverConfig)) {
        return {};
    }
    for (const QJsonValue &value : entries(serverConfig)) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("ip")).toString().trimmed() == ip) {
            return entry;
        }
    }
    return {};
}

// Port declared for one address, or 0 when none is (caller then uses the
// protocol port — the same fallback the legacy `node_ports` map had).
inline int portFor(const QJsonObject &serverConfig, const QString &ip)
{
    const QJsonObject entry = entryFor(serverConfig, ip);
    if (entry.isEmpty()) {
        return 0;
    }
    const QJsonValue raw = entry.value(QStringLiteral("port"));
    const int port = raw.isString() ? raw.toString().toInt() : raw.toInt();
    return (port > 0 && port <= 65535) ? port : 0;
}

// Weight of one address: 0/negative/absent all mean "not set" (100), exactly
// like the legacy `node_weights` map — a node on an old build sends 0 instead
// of omitting the field.
inline int weightFor(const QJsonObject &serverConfig, const QString &ip)
{
    const QJsonObject entry = entryFor(serverConfig, ip);
    if (entry.isEmpty()) {
        return kDefaultWeight;
    }
    const QJsonValue raw = entry.value(QStringLiteral("weight"));
    const int weight = raw.isString() ? raw.toString().toInt() : raw.toInt();
    return weight > 0 ? weight : kDefaultWeight;
}

// Derives the legacy keys from `endpoints[]` so the rest of the client and the
// stored row keep one source of truth. A no-op for a payload without
// `endpoints` — an old server row is left byte-identical.
//
// `node_ips` is always written (the picker iterates it), `node_ports` only when
// at least one address declares its own port, `node_weights` only when at least
// one weight differs from the default (keeps the stored JSON small, and matches
// what the api omits for a plain node).
inline void applyLegacy(QJsonObject &serverConfig)
{
    if (!present(serverConfig)) {
        return;
    }
    const QStringList addresses = ips(serverConfig);

    QJsonArray nodeIps;
    QJsonObject ports;
    QJsonObject weights;
    bool anyPort = false;
    bool anyWeight = false;
    for (const QString &ip : addresses) {
        nodeIps.append(ip);
        const int port = portFor(serverConfig, ip);
        if (port > 0) {
            ports[ip] = port;
            anyPort = true;
        }
        const int weight = weightFor(serverConfig, ip);
        if (weight != kDefaultWeight) {
            weights[ip] = weight;
            anyWeight = true;
        }
    }

    serverConfig[QStringLiteral("node_ips")] = nodeIps;
    if (anyPort) {
        serverConfig[QStringLiteral("node_ports")] = ports;
    } else {
        serverConfig.remove(QStringLiteral("node_ports"));
    }
    if (anyWeight) {
        serverConfig[QStringLiteral("node_weights")] = weights;
    } else {
        serverConfig.remove(QStringLiteral("node_weights"));
    }
}

} // namespace NodeEndpoints
