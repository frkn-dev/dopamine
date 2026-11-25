#include "ui/controllers/subscriptionsInfoController.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <QtConcurrent/QtConcurrent>

#include "core/api/apiDefs.h"
#include "core/controllers/gatewayController.h"
#include "core/controllers/peersController.h"
#include "core/defs.h"
#include "dopamine_application.h"
#include "settings.h"
#include "ui/models/servers_model.h"
#include "version.h"

namespace
{
namespace configKey
{
constexpr char apiConfig[] = "api_config";
constexpr char authData[] = "auth_data";
constexpr char apiKey[] = "api_key";
constexpr char shareToken[] = "share_token";
constexpr char serviceType[] = "service_type";
constexpr char userCountryCode[] = "user_country_code";
constexpr char peerSubscriptionId[] = "peerSubscriptionId";
} // namespace configKey

// One subscription as the api sees it, before it becomes a QML row.
struct SubscriptionSource
{
    QString id;
    QString serviceType;
    QString userCountryCode;
    bool isTestPurchase = false;
};

struct SubscriptionRow
{
    QString id;
    QString code;
    QString planTitle;
    QString expiresText;
    QString usedText;
    QString limitText; // empty: no limit
    bool expired = false;
    bool hasDetails = false;
    bool hasTraffic = false;
};

QString formatGb(qint64 bytes)
{
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib < 1.0) {
        const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
        return QStringLiteral("%1 MB").arg(QString::number(mib, 'f', mib < 10.0 ? 1 : 0));
    }
    return QStringLiteral("%1 GB").arg(QString::number(gib, 'f', gib < 10.0 ? 1 : 0));
}

// `traffic_limit: null` means "no limit" on the peers plane: PeersController
// keeps it as -1.
QString formatLimit(qint64 limit)
{
    if (limit < 0) {
        return {};
    }
    return formatGb(limit);
}

// Plain (not enveloped) GET: the share code is served by a public endpoint.
QString fetchShortCode(const QString &subscriptionId)
{
    QNetworkAccessManager manager; // lives in the worker thread on purpose
    QNetworkRequest request(QUrl(QStringLiteral("https://api.frkn.org/short?subscription_id=") + subscriptionId));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(10000);

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // Transfer timeout does not fire on every platform before the socket is up.
    QTimer::singleShot(12000, &loop, &QEventLoop::quit);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
    }
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[SUBSCRIPTIONS] short code fetch failed:" << reply->errorString();
        reply->deleteLater();
        return {};
    }

    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    QString code = body.value(QStringLiteral("code")).toString();
    if (code.isEmpty()) {
        code = body.value(QStringLiteral("short_code")).toString();
    }
    return code;
}

// Old v1 subscriptions: expiry (and traffic, when the backend sends it) come
// from account_info. Same payload shape as ApiSettingsController::getAccountInfo.
bool fetchAccountInfo(Settings &settings, QSharedPointer<GatewayController> &gateway,
                      const SubscriptionSource &source, SubscriptionRow &row)
{
    QJsonObject authData;
    authData[QLatin1String(configKey::apiKey)] = source.id;
    authData[QStringLiteral("id")] = source.id;

    QJsonObject payload;
    payload[QStringLiteral("user_country_code")] = source.userCountryCode;
    payload[QStringLiteral("service_type")] = source.serviceType;
    payload[QStringLiteral("auth_data")] = authData;
    payload[QStringLiteral("cli_version")] = QString(APP_VERSION);
    payload[QStringLiteral("app_language")] = settings.getAppLanguage().name().split("_").first();

    QByteArray body;
    if (gateway->post(QStringLiteral("%1v1/account_info"), payload, body) != ErrorCode::NoError) {
        return false;
    }

    const QJsonObject info = QJsonDocument::fromJson(body).object();
    if (info.isEmpty()) {
        return false;
    }

    row.planTitle = info.value(QStringLiteral("subscription_description")).toString();

    const QString endDate = info.value(QStringLiteral("subscription_end_date")).toString();
    QDateTime expiresAt = QDateTime::fromString(endDate, Qt::ISODateWithMs);
    if (!expiresAt.isValid()) {
        expiresAt = QDateTime::fromString(endDate, Qt::ISODate);
    }
    if (expiresAt.isValid()) {
        const QDateTime local = expiresAt.toLocalTime();
        row.expiresText = local.toString(QStringLiteral("d MMM yyyy"));
        row.expired = local < QDateTime::currentDateTime();
    }

    // The v1 payload is not guaranteed to carry traffic counters; read the
    // names the backend has used so far and leave the line empty otherwise.
    auto bytesOf = [&info](const QStringList &keys, bool &found) {
        for (const QString &key : keys) {
            const QJsonValue value = info.value(key);
            if (value.isUndefined()) {
                continue;
            }
            found = true;
            if (value.isNull()) {
                return qint64(-1); // null limit / unknown counter
            }
            return static_cast<qint64>(value.toDouble(0));
        }
        return qint64(-1);
    };

    bool hasUsed = false;
    // Total traffic of the subscription, all time, all envs — the number the
    // cabinet shows. `downlink`+`uplink` is the full pipe; the per-grant
    // `used_bytes` (day-only) would read as a few MB next to the cabinet's GB.
    qint64 used = bytesOf({ QStringLiteral("downlink"), QStringLiteral("used_bytes"), QStringLiteral("traffic_used") }, hasUsed);
    const qint64 uplink = bytesOf({ QStringLiteral("uplink") }, hasUsed);
    if (uplink > 0) {
        used += uplink;
    }
    bool hasLimit = false;
    const qint64 limit = bytesOf({ QStringLiteral("traffic_limit"), QStringLiteral("traffic_limit_bytes") },
                                 hasLimit);

    if (hasUsed || hasLimit) {
        row.hasTraffic = true;
        row.usedText = formatGb(used > 0 ? used : 0);
    }
    if (hasLimit) {
        row.limitText = limit > 0 ? formatGb(limit) : QString();
    }

    row.hasDetails = true;
    return true;
}

SubscriptionRow fetchSubscription(Settings &settings, const SubscriptionSource &source)
{
    SubscriptionRow row;
    row.id = source.id;

    auto gateway = QSharedPointer<GatewayController>::create(
            settings.getGatewayEndpoint(source.isTestPurchase), settings.isDevGatewayEnv(source.isTestPurchase),
            apiDefs::requestTimeoutMsecs, false);

    amnezia::peers::PeersController peers(gateway.get());
    QVector<amnezia::peers::PoolCard> cards;
    QVector<amnezia::peers::AccessInfo> access;
    const auto code = peers.locations(source.id, cards, access);
    if (code == ErrorCode::NoError && !access.isEmpty()) {
        const auto &info = access.first();
        row.planTitle = info.planTitle.isEmpty() ? info.plan : info.planTitle;
        if (info.expiresAt.isValid()) {
            const QDateTime local = info.expiresAt.toLocalTime();
            row.expiresText = local.toString(QStringLiteral("d MMM yyyy"));
            row.expired = local < QDateTime::currentDateTime();
        }
        row.hasTraffic = true;
        // The peers plane counts used_bytes per grant per day — a few MB for a
        // fresh day. The cabinet shows the subscription's total, so the app
        // fetches the same `GET /subscription/{id}` and takes `downlink`+`uplink`
        // from it instead of summing the day counters.
        qint64 totalBytes = info.usedBytes > 0 ? info.usedBytes : 0;
        {
            QNetworkAccessManager manager;
            QNetworkRequest request(QUrl(QStringLiteral("https://api.frkn.org/subscription/") + source.id));
            request.setRawHeader("Accept", "application/json");
            request.setTransferTimeout(10000);
            QNetworkReply *reply = manager.get(request);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(12000, &loop, &QEventLoop::quit);
            loop.exec();
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonObject sub = QJsonDocument::fromJson(reply->readAll()).object();
                const qint64 downlink = static_cast<qint64>(sub.value(QStringLiteral("downlink")).toDouble(0));
                const qint64 uplink = static_cast<qint64>(sub.value(QStringLiteral("uplink")).toDouble(0));
                if (downlink + uplink > 0) {
                    totalBytes = downlink + uplink;
                }
            }
            reply->deleteLater();
        }
        row.usedText = formatGb(totalBytes);
        row.limitText = formatLimit(info.trafficLimit);
        row.hasDetails = true;
    } else {
        // No peers grant: an old v1 subscription, ask account_info instead.
        fetchAccountInfo(settings, gateway, source, row);
    }

    row.code = fetchShortCode(source.id);
    return row;
}

QVariantMap rowToMap(const SubscriptionRow &row)
{
    QVariantMap map;
    map[QStringLiteral("id")] = row.id;
    map[QStringLiteral("code")] = row.code;
    map[QStringLiteral("planTitle")] = row.planTitle;
    map[QStringLiteral("expiresText")] = row.expiresText;
    map[QStringLiteral("expired")] = row.expired;
    map[QStringLiteral("usedText")] = row.usedText;
    map[QStringLiteral("limitText")] = row.limitText;
    map[QStringLiteral("hasTraffic")] = row.hasTraffic;
    map[QStringLiteral("hasDetails")] = row.hasDetails;
    return map;
}
} // namespace

SubscriptionsInfoController::SubscriptionsInfoController(QSharedPointer<ServersModel> serversModel,
                                                         std::shared_ptr<Settings> settings, QObject *parent)
    : QObject(parent), m_serversModel(std::move(serversModel)), m_settings(std::move(settings))
{
}

void SubscriptionsInfoController::refresh()
{
    if (!m_serversModel || !m_settings || m_busy) {
        return;
    }

    // Collecting the ids touches the model: GUI thread only. The list is
    // rebuilt from scratch every time, so a subscription that was replaced or
    // removed since the last refresh cannot leak its id into the new list.
    m_subscriptions.clear();
    emit subscriptionsChanged();

    QVector<SubscriptionSource> sources;
    QSet<QString> seen;
    for (int i = 0; i < m_serversModel->rowCount(); ++i) {
        const QJsonObject row = m_serversModel->serverRow(i);
        const QJsonObject apiConfig = row.value(QLatin1String(configKey::apiConfig)).toObject();
        const QJsonObject apiAuthData = apiConfig.value(QLatin1String(configKey::authData)).toObject();
        const QJsonObject rootAuthData = row.value(QLatin1String(configKey::authData)).toObject();

        // Shared connections are somebody else's subscription: they carry a
        // share_token and are not listed here.
        if (apiAuthData.contains(QLatin1String(configKey::shareToken))
            || rootAuthData.contains(QLatin1String(configKey::shareToken))) {
            continue;
        }

        QString id = row.value(QLatin1String(configKey::peerSubscriptionId)).toString();
        if (id.isEmpty()) {
            id = apiAuthData.value(QLatin1String(configKey::apiKey)).toString();
        }
        if (id.isEmpty()) {
            id = rootAuthData.value(QLatin1String(configKey::apiKey)).toString();
        }
        if (id.isEmpty() || seen.contains(id)) {
            continue;
        }
        seen.insert(id);

        SubscriptionSource source;
        source.id = id;
        source.serviceType = apiConfig.value(QLatin1String(configKey::serviceType)).toString();
        source.userCountryCode = apiConfig.value(QLatin1String(configKey::userCountryCode)).toString();
        source.isTestPurchase = apiConfig.value(apiDefs::key::isTestPurchase).toBool(false);
        sources.append(source);
    }

    if (sources.isEmpty()) {
        m_subscriptions.clear();
        emit subscriptionsChanged();
        return;
    }

    setBusy(true);
    auto settings = m_settings;

    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher]() {
        const QVariantList rows = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        applyRows(rows);
    });

    watcher->setFuture(QtConcurrent::run([settings, sources]() {
        QVariantList rows;
        for (const auto &source : sources) {
            rows.append(rowToMap(fetchSubscription(*settings, source)));
        }
        return rows;
    }));
}

void SubscriptionsInfoController::applyRows(const QVariantList &rows)
{
    m_subscriptions = rows;
    emit subscriptionsChanged();
}

void SubscriptionsInfoController::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

void SubscriptionsInfoController::copyToClipboard(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    amnApp->getClipboard()->setText(text);
}
