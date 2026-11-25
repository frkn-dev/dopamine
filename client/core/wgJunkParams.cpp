#include "wgJunkParams.h"

#include <QJsonArray>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

// AWG junk parameters must be sent around the handshake, otherwise the server
// stays silent. Collected from last_config (long and short names) and topped up
// from the INI where fields are empty.
QJsonObject wgJunkParamsFromConfig(const QJsonObject &lastConfig, const QString &ini)
{
    auto jsonText = [](const QJsonValue &value) {
        if (value.isString()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return QString::number(static_cast<qint64>(value.toDouble()));
        }
        return QString();
    };
    QJsonObject params = lastConfig;
    const QList<QPair<QString, QString>> aliases = {
        { QStringLiteral("junkPacketCount"), QStringLiteral("Jc") },
        { QStringLiteral("junkPacketMinSize"), QStringLiteral("Jmin") },
        { QStringLiteral("junkPacketMaxSize"), QStringLiteral("Jmax") },
        { QStringLiteral("initPacketJunkSize"), QStringLiteral("S1") },
        { QStringLiteral("responsePacketJunkSize"), QStringLiteral("S2") },
        { QStringLiteral("cookieReplyPacketJunkSize"), QStringLiteral("S3") },
        { QStringLiteral("transportPacketJunkSize"), QStringLiteral("S4") },
        { QStringLiteral("initPacketMagicHeader"), QStringLiteral("H1") },
        { QStringLiteral("responsePacketMagicHeader"), QStringLiteral("H2") },
        { QStringLiteral("underloadPacketMagicHeader"), QStringLiteral("H3") },
        { QStringLiteral("transportPacketMagicHeader"), QStringLiteral("H4") },
        { QStringLiteral("specialJunk1"), QStringLiteral("I1") },
        { QStringLiteral("specialJunk2"), QStringLiteral("I2") },
        { QStringLiteral("specialJunk3"), QStringLiteral("I3") },
        { QStringLiteral("specialJunk4"), QStringLiteral("I4") },
        { QStringLiteral("specialJunk5"), QStringLiteral("I5") },
        { QStringLiteral("headerProtectionKey"), QStringLiteral("HeaderProtectionKey") },
    };
    for (const auto &alias : aliases) {
        if (!jsonText(params.value(alias.second)).isEmpty()) {
            continue;
        }
        const QString value = jsonText(params.value(alias.first));
        if (!value.isEmpty()) {
            params.insert(alias.second, value);
        }
    }
    QJsonObject junk;
    for (const char *key : { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4", "H1", "H2", "H3", "H4",
                             "I1", "I2", "I3", "I4", "I5", "HeaderProtectionKey" }) {
        const QString value = jsonText(params.value(QString::fromLatin1(key)));
        if (!value.isEmpty()) {
            junk.insert(QString::fromLatin1(key), value);
        }
    }

    // fill gaps from junk params embedded in the wg-quick INI (some configs
    // carry them only there / last_config fields may be null)
    static const QSet<QString> junkKeys = { "Jc", "Jmin", "Jmax", "S1", "S2", "S3", "S4",
                                            "H1", "H2", "H3", "H4", "I1", "I2", "I3", "I4", "I5",
                                            "HeaderProtectionKey" };
    for (const QString &line : ini.split(QLatin1Char('\n'))) {
        const auto parts = line.split(QLatin1Char('='), Qt::SkipEmptyParts);
        if (parts.size() != 2) {
            continue;
        }
        const QString key = parts[0].trimmed();
        if (junkKeys.contains(key) && !junk.contains(key)) {
            const QString value = parts[1].trimmed();
            if (!value.isEmpty()) {
                junk.insert(key, value);
            }
        }
    }

    return junk;
}
