#ifndef APIUTILS_H
#define APIUTILS_H

#include <QNetworkReply>
#include <QObject>

#include "apiDefs.h"
#include "core/defs.h"

namespace apiUtils
{
    bool isServerFromApi(const QJsonObject &serverConfigObject);

    bool isSubscriptionExpired(const QString &subscriptionEndDate);

    bool isPremiumServer(const QJsonObject &serverConfigObject);

    apiDefs::ConfigType getConfigType(const QJsonObject &serverConfigObject);
    apiDefs::ConfigSource getConfigSource(const QJsonObject &serverConfigObject);

    amnezia::ErrorCode checkNetworkReplyErrors(const QList<QSslError> &sslErrors, const QString &replyErrorString,
                                               const QNetworkReply::NetworkError &replyError, const int httpStatusCode,
                                               const QByteArray &responseBody);

    QString getPremiumV1VpnKey(const QJsonObject &serverConfigObject);
    QString getPremiumV2VpnKey(const QJsonObject &serverConfigObject);

    // Translates a legacy hysteria2 outbound (protocol "hysteria2", settings.servers[],
    // network "udp") to the schema the current amnezia-xray-core accepts
    // (protocol "hysteria" + network "hysteria" + hysteriaSettings.auth, ALPN h3).
    // Non-hysteria2 outbounds pass through unchanged.
    QJsonObject translateLegacyHysteria2Outbound(const QJsonObject &outbound);
}

#endif // APIUTILS_H
