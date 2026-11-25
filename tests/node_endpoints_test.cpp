// Contract test for the ONE node endpoint shape on the client side
// (client/ui/models/node_endpoints.h).
//
// The client has no unit-test harness of its own, so this is a standalone
// QtCore-only program: it compiles the very header the app uses and pins the
// two promises of the change —
//
//   (e) an OLD payload (node_ips/node_ports/node_weights, no `endpoints`) is
//       left byte-identical and still yields the same addresses, ports and
//       weights (the address picker's inputs);
//   (n) a NEW payload (`endpoints[]`) produces exactly the legacy triple the
//       picker already understands, so "weights as share, ping as veto"
//       behaves identically for both sources.
//
// Build & run (macOS, Qt from the repo's toolchain):
//   QT=~/c/6.10.1/6.10.1/macos
//   clang++ -std=c++20 -fPIC tests/node_endpoints_test.cpp \
//     -I client/ui/models -I $QT/lib/QtCore.framework/Headers \
//     -F $QT/lib -framework QtCore -Wl,-rpath,$QT/lib \
//     -o /tmp/node_endpoints_test && /tmp/node_endpoints_test
//
// The file lives outside `client/` on purpose: `client/CMakeLists.txt` globs
// every `*.cpp` under that tree into the app target, and a second `main()`
// would break the build.
//
// Exit code 0 = every assertion passed.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

#include "node_endpoints.h"

namespace {

int g_failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void checkEqual(const QString &got, const QString &want, const char *what)
{
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: got %s, want %s\n", what, qPrintable(got),
                     qPrintable(want));
        ++g_failures;
    }
}

void checkEqual(int got, int want, const char *what)
{
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: got %d, want %d\n", what, got, want);
        ++g_failures;
    }
}

QJsonObject parse(const char *json)
{
    QJsonParseError error {};
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &error);
    if (error.error != QJsonParseError::NoError) {
        std::fprintf(stderr, "FAIL: test fixture is not valid JSON: %s\n",
                     qPrintable(error.errorString()));
        std::exit(2);
    }
    return doc.object();
}

QString serialized(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// An old gateway payload: exactly the three legacy fields, no `endpoints`.
QJsonObject legacyPayload()
{
    return parse(R"({
        "hostName": "82.148.231.161",
        "node_ips": ["82.148.231.161", "82.148.231.162"],
        "node_ports": {"82.148.231.162": 8443},
        "node_weights": {"82.148.231.161": 200},
        "api_config": {"node_id": "n1"}
    })");
}

// The same node on the new shape, plus the legacy mirror the api also sends.
QJsonObject endpointPayload(bool withLegacyMirror)
{
    QJsonObject object = parse(R"({
        "hostName": "82.148.231.161",
        "endpoints": [
            {"ip": "82.148.231.161", "port": 443, "weight": 200},
            {"ip": "82.148.231.162", "port": 8443, "weight": 100, "label": "nl-02"}
        ],
        "api_config": {"node_id": "n1"}
    })");
    if (withLegacyMirror) {
        object["node_ips"] = parse(R"({"x": ["9.9.9.9"]})").value("x");
        object["node_ports"] = parse(R"({"x": {"9.9.9.9": 1234}})" ).value("x");
        object["node_weights"] = parse(R"({"x": {"9.9.9.9": 7}})").value("x");
    }
    return object;
}

void testOldPayloadIsUntouched()
{
    QJsonObject config = legacyPayload();
    const QString before = serialized(config);
    NodeEndpoints::applyLegacy(config);
    checkEqual(serialized(config), before, "(e) old payload must stay byte-identical");
    check(!NodeEndpoints::present(config), "(e) old payload has no endpoints");

    checkEqual(NodeEndpoints::ips(config).join(','), QStringLiteral("82.148.231.161,82.148.231.162"),
               "(e) legacy ips");
    // Same values the picker feeds into the weights-as-share / ping-as-veto
    // selection: the endpoints path must not drift from these.
    checkEqual(NodeEndpoints::portFor(config, QStringLiteral("82.148.231.161")), 0,
               "legacy address without node_ports entry: caller uses the protocol port");
    checkEqual(NodeEndpoints::weightFor(config, QStringLiteral("82.148.231.161")),
               NodeEndpoints::kDefaultWeight, "legacy config has no endpoint weights");
    checkEqual(config.value("node_ports").toObject().value("82.148.231.162").toInt(), 8443,
               "legacy node_ports preserved");
}

void testNewPayloadDerivesTheLegacyTriple()
{
    QJsonObject config = endpointPayload(false);
    NodeEndpoints::applyLegacy(config);

    checkEqual(config.value("node_ips").toArray().size(), 2, "(n) node_ips derived");
    checkEqual(config.value("node_ips").toArray().at(1).toString(), QStringLiteral("82.148.231.162"),
               "(n) node_ips order follows endpoints[]");
    checkEqual(config.value("node_ports").toObject().value("82.148.231.162").toInt(), 8443,
               "(n) declared port reaches node_ports");
    checkEqual(config.value("node_ports").toObject().value("82.148.231.161").toInt(), 443,
               "(n) explicit port 443 is kept");
    checkEqual(config.value("node_weights").toObject().value("82.148.231.161").toInt(), 200,
               "(n) weight reaches node_weights");

    // The same lookups the picker performs per address.
    checkEqual(NodeEndpoints::portFor(config, QStringLiteral("82.148.231.162")), 8443,
               "(n) portFor reads endpoints");
    checkEqual(NodeEndpoints::weightFor(config, QStringLiteral("82.148.231.161")), 200,
               "(n) weightFor reads endpoints");
    checkEqual(NodeEndpoints::weightFor(config, QStringLiteral("82.148.231.162")),
               NodeEndpoints::kDefaultWeight, "(n) missing weight means 100");
}

// The api sends both shapes at once: `endpoints` must win over a stale mirror,
// and a weight of 0/negative is "not set" exactly like the legacy map.
void testEndpointsWinOverTheLegacyMirror()
{
    QJsonObject config = endpointPayload(true);
    NodeEndpoints::applyLegacy(config);

    checkEqual(NodeEndpoints::ips(config).join(','), QStringLiteral("82.148.231.161,82.148.231.162"),
               "endpoints wins over node_ips");
    checkEqual(NodeEndpoints::weightFor(config, QStringLiteral("82.148.231.161")), 200,
               "endpoints weight wins over node_weights");
    checkEqual(config.value("node_weights").toObject().value("9.9.9.9").toInt(), 0,
               "stale mirror keys are replaced, not merged");

    QJsonObject zero = parse(R"({"endpoints": [
        {"ip": "10.0.0.1", "port": 0, "weight": 0}
    ]})");
    checkEqual(NodeEndpoints::portFor(zero, QStringLiteral("10.0.0.1")), 0,
               "port 0 means 'the protocol port'");
    checkEqual(NodeEndpoints::weightFor(zero, QStringLiteral("10.0.0.1")),
               NodeEndpoints::kDefaultWeight, "weight 0 means 100");
    NodeEndpoints::applyLegacy(zero);
    check(!zero.contains("node_ports"), "no real port -> node_ports removed");
    check(!zero.contains("node_weights"), "no real weight -> node_weights removed");
}

// A share payload may carry only `endpoints` (no node_ips at all): the picker
// must still see both addresses.
void testEndpointsWithoutAnyLegacyKey()
{
    QJsonObject config = parse(R"({"endpoints": [
        {"ip": "10.1.0.1"}, {"ip": "10.1.0.2", "weight": 25}
    ]})");
    checkEqual(NodeEndpoints::ips(config).join(','), QStringLiteral("10.1.0.1,10.1.0.2"),
               "ips from endpoints alone");
    NodeEndpoints::applyLegacy(config);
    checkEqual(config.value("node_ips").toArray().size(), 2, "node_ips written");
    checkEqual(config.value("node_weights").toObject().value("10.1.0.2").toInt(), 25,
               "only non-default weights are written");
    check(!config.value("node_weights").toObject().contains("10.1.0.1"),
          "default weight is not written");
}

// A single-address card row (no node_ips at all, dialled by hostName) must stay
// untouched — `applyLegacy` on an old payload is a pure no-op.
void testSingleAddressRowIsUntouched()
{
    QJsonObject config = parse(R"({"hostName": "nl-01.frkn.org", "port": 443})");
    const QString before = serialized(config);
    NodeEndpoints::applyLegacy(config);
    checkEqual(serialized(config), before, "single-address old row is untouched");
    check(NodeEndpoints::ips(config).isEmpty(), "no ips -> caller dials hostName");
}

} // namespace

int main()
{
    testOldPayloadIsUntouched();
    testNewPayloadDerivesTheLegacyTriple();
    testEndpointsWinOverTheLegacyMirror();
    testEndpointsWithoutAnyLegacyKey();
    testSingleAddressRowIsUntouched();

    if (g_failures == 0) {
        std::printf("node_endpoints_test: all assertions passed\n");
        return 0;
    }
    std::fprintf(stderr, "node_endpoints_test: %d failure(s)\n", g_failures);
    return 1;
}
