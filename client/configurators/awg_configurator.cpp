#include "awg_configurator.h"
#include "protocols/protocols_defs.h"

#include <QJsonDocument>
#include <QJsonObject>

AwgConfigurator::AwgConfigurator(std::shared_ptr<Settings> settings, const QSharedPointer<ServerController> &serverController, QObject *parent)
    : WireguardConfigurator(settings, serverController, true, parent)
{
}

QString AwgConfigurator::createConfig(const ServerCredentials &credentials, DockerContainer container, const QJsonObject &containerConfig,
                                      ErrorCode &errorCode)
{
    QString config = WireguardConfigurator::createConfig(credentials, container, containerConfig, errorCode);

    QJsonObject jsonConfig = QJsonDocument::fromJson(config.toUtf8()).object();
    QString awgConfig = jsonConfig.value(config_key::config).toString();

    QMap<QString, QString> configMap;
    auto configLines = awgConfig.split("\n");
    for (auto &line : configLines) {
        auto trimmedLine = line.trimmed();
        if (trimmedLine.startsWith("[") && trimmedLine.endsWith("]")) {
            continue;
        } else {
            QStringList parts = trimmedLine.split(" = ");
            if (parts.count() == 2) {
                configMap.insert(parts[0].trimmed(), parts[1].trimmed());
            }
        }
    }

    jsonConfig[config_key::junkPacketCount] = configMap.value(config_key::junkPacketCount);
    jsonConfig[config_key::junkPacketMinSize] = configMap.value(config_key::junkPacketMinSize);
    jsonConfig[config_key::junkPacketMaxSize] = configMap.value(config_key::junkPacketMaxSize);
    jsonConfig[config_key::initPacketJunkSize] = configMap.value(config_key::initPacketJunkSize);
    jsonConfig[config_key::responsePacketJunkSize] = configMap.value(config_key::responsePacketJunkSize);
    jsonConfig[config_key::initPacketMagicHeader] = configMap.value(config_key::initPacketMagicHeader);
    jsonConfig[config_key::responsePacketMagicHeader] = configMap.value(config_key::responsePacketMagicHeader);
    jsonConfig[config_key::underloadPacketMagicHeader] = configMap.value(config_key::underloadPacketMagicHeader);
    jsonConfig[config_key::transportPacketMagicHeader] = configMap.value(config_key::transportPacketMagicHeader);

    if (container == DockerContainer::Awg2) {
        jsonConfig[config_key::cookieReplyPacketJunkSize] = configMap.value(config_key::cookieReplyPacketJunkSize);
        jsonConfig[config_key::transportPacketJunkSize] = configMap.value(config_key::transportPacketJunkSize);
    }

    jsonConfig[config_key::specialJunk1] = configMap.value(amnezia::config_key::specialJunk1);
    jsonConfig[config_key::specialJunk2] = configMap.value(amnezia::config_key::specialJunk2);
    jsonConfig[config_key::specialJunk3] = configMap.value(amnezia::config_key::specialJunk3);
    jsonConfig[config_key::specialJunk4] = configMap.value(amnezia::config_key::specialJunk4);
    jsonConfig[config_key::specialJunk5] = configMap.value(amnezia::config_key::specialJunk5);

    // AWG 3.1 optional booleans ("on"/"off")
    if (configMap.contains(config_key::randomTrailers)) {
        jsonConfig[config_key::randomTrailers] = configMap.value(config_key::randomTrailers);
    }
    if (configMap.contains(config_key::disableCookies)) {
        jsonConfig[config_key::disableCookies] = configMap.value(config_key::disableCookies);
    }

    // AWG 3.0 optional device-level keys (values may be ints or "lo-hi" ranges)
    for (const auto &key : { config_key::headerProtectionKey, config_key::contentPaddingAddition,
                             config_key::rekeyAfterTime, config_key::rekeyTimeout,
                             config_key::rejectAfterTime, config_key::keepaliveTimeout,
                             config_key::maxHandshakeAttempts }) {
        if (configMap.contains(key)) {
            jsonConfig[key] = configMap.value(key);
        }
    }

    jsonConfig[config_key::mtu] =
            containerConfig.value(ProtocolProps::protoToString(Proto::Awg)).toObject().value(config_key::mtu).toString(protocols::awg::defaultMtu);

    return QJsonDocument(jsonConfig).toJson();
}
