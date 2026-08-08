#include "apiConfigsController.h"

#include "amnezia_application.h"
#include "configurators/wireguard_configurator.h"
#include "containers/containers_defs.h"
#include "core/api/apiDefs.h"
#include "core/api/apiUtils.h"
#include "core/controllers/gatewayController.h"
#include "core/qrCodeUtils.h"
#include "protocols/protocols_defs.h"
#include "ui/controllers/systemController.h"
#include "version.h"
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include "platforms/ios/ios_controller.h"

#include <zlib.h>

namespace
{
    static QByteArray gzipDecompress(const QByteArray &data)
    {
        if (data.size() < 10)
            return {};

        constexpr int CHUNK = 16384;
        z_stream strm = {};
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = static_cast<uInt>(data.size());
        strm.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data()));

        // 15 (max window bits) + 16 (enable gzip decoding)
        int ret = inflateInit2(&strm, 15 + 16);
        if (ret != Z_OK)
            return {};

        QByteArray out;
        char outBuffer[CHUNK];
        do {
            strm.avail_out = CHUNK;
            strm.next_out = reinterpret_cast<Bytef *>(outBuffer);
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&strm);
                return {};
            }
            out.append(outBuffer, CHUNK - strm.avail_out);
        } while (ret != Z_STREAM_END);

        inflateEnd(&strm);
        return out;
    }

    namespace configKey
    {
        constexpr char cloak[] = "cloak";
        constexpr char awg[] = "awg";
        constexpr char vless[] = "vless";
        constexpr char wireguard[] = "wireguard";

        constexpr char apiEndpoint[] = "api_endpoint";
        constexpr char accessToken[] = "api_key";
        constexpr char certificate[] = "certificate";
        constexpr char publicKey[] = "public_key";
        constexpr char protocol[] = "protocol";

        constexpr char uuid[] = "installation_uuid";
        constexpr char osVersion[] = "os_version";
        constexpr char appVersion[] = "app_version";

        constexpr char userCountryCode[] = "user_country_code";
        constexpr char serverCountryCode[] = "server_country_code";
        constexpr char countryCode[] = "country_code";
        constexpr char services[] = "services";
        constexpr char availableCountries[] = "available_countries";
        constexpr char serviceType[] = "service_type";
        constexpr char serviceInfo[] = "service_info";
        constexpr char serviceProtocol[] = "service_protocol";

        constexpr char productId[] = "product_id";

        constexpr char apiPayload[] = "api_payload";
        constexpr char keyPayload[] = "key_payload";

        constexpr char apiConfig[] = "api_config";
        constexpr char authData[] = "auth_data";

        constexpr char config[] = "config";

        constexpr char subscription[] = "subscription";
        constexpr char endDate[] = "end_date";

        constexpr char isConnectEvent[] = "is_connect_event";
    }

    namespace serviceType
    {
        constexpr char amneziaFree[] = "amnezia-free";
        constexpr char amneziaPremium[] = "amnezia-premium";
    }

    struct SubscriptionProductInfo
    {
        QString productId;
        int months;
    };

    const QList<SubscriptionProductInfo> subscriptionProducts = { { QStringLiteral("frkn_premium_1_month"), 1 },
                                                                  { QStringLiteral("frkn_premium_3_month"), 3 },
                                                                  { QStringLiteral("frkn_premium_6_month"), 6 },
                                                                  { QStringLiteral("frkn_premium_12_month"), 12 } };

    const int defaultSubscriptionPlanIndex = 2; // frkn_premium_6_month

    struct GatewayRequestData
    {
        QString osVersion;
        QString appVersion;
        QString appLanguage;

        QString installationUuid;

        QString userCountryCode;
        QString serverCountryCode;
        QString serviceType;
        QString serviceProtocol;

        QJsonObject authData;

        QJsonObject toJsonObject() const
        {
            QJsonObject obj;
            if (!osVersion.isEmpty()) {
                obj[configKey::osVersion] = osVersion;
            }
            if (!appVersion.isEmpty()) {
                obj[configKey::appVersion] = appVersion;
            }
            if (!appLanguage.isEmpty()) {
                obj[apiDefs::key::appLanguage] = appLanguage;
            }
            if (!installationUuid.isEmpty()) {
                obj[configKey::uuid] = installationUuid;
            }
            if (!userCountryCode.isEmpty()) {
                obj[configKey::userCountryCode] = userCountryCode;
            }
            if (!serverCountryCode.isEmpty()) {
                obj[configKey::serverCountryCode] = serverCountryCode;
            }
            if (!serviceType.isEmpty()) {
                obj[configKey::serviceType] = serviceType;
            }
            if (!serviceProtocol.isEmpty()) {
                obj[configKey::serviceProtocol] = serviceProtocol;
            }
            obj[configKey::authData] = authData;
            return obj;
        }
    };

    ProtocolData generateProtocolData(const QString &protocol)
    {
        ProtocolData protocolData;
        if (protocol == configKey::cloak) {
            protocolData.certRequest = OpenVpnConfigurator::createCertRequest();
        } else if (protocol == configKey::awg || protocol == configKey::wireguard) {
            auto connData = WireguardConfigurator::genClientKeys();
            protocolData.wireGuardClientPubKey = connData.clientPubKey;
            protocolData.wireGuardClientPrivKey = connData.clientPrivKey;
        } else if (protocol == configKey::vless) {
            protocolData.xrayUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        return protocolData;
    }

    void appendProtocolDataToApiPayload(const QString &protocol, const ProtocolData &protocolData, QJsonObject &apiPayload)
    {
        if (protocol == configKey::cloak) {
            apiPayload[configKey::certificate] = protocolData.certRequest.request;
        } else if (protocol == configKey::awg || protocol == configKey::wireguard) {
            apiPayload[configKey::publicKey] = protocolData.wireGuardClientPubKey;
        } else if (protocol == configKey::vless) {
            apiPayload[configKey::publicKey] = protocolData.xrayUuid;
        }
    }

    ErrorCode fillServerConfig(const QString &protocol, const ProtocolData &apiPayloadData, const QByteArray &apiResponseBody,
                               QJsonObject &serverConfig)
    {
        QString data = QJsonDocument::fromJson(apiResponseBody).object().value(config_key::config).toString();

        data.replace("vpn://", "");

        // The backend may return standard base64 (with +/=) or URL-safe base64 (with -_ and no padding).
        QByteArray ba = QByteArray::fromBase64(data.toUtf8());
        if (ba.isEmpty() || (!ba.startsWith('{') && ba.left(2) != QByteArray("\x1f\x8b", 2))) {
            ba = QByteArray::fromBase64(data.toUtf8(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        }

        if (ba.isEmpty()) {
            qDebug() << "empty vpn key";
            return ErrorCode::ApiConfigEmptyError;
        }

        // The payload may be plain JSON, zlib-compressed (qUncompress) or gzip-compressed.
        QByteArray ba_uncompressed = qUncompress(ba);
        if (ba_uncompressed.isEmpty()) {
            ba_uncompressed = gzipDecompress(ba);
        }
        if (!ba_uncompressed.isEmpty()) {
            ba = ba_uncompressed;
        }

        QString configStr = ba;
        if (protocol == configKey::cloak) {
            configStr.replace("<key>", "<key>\n");
            configStr.replace("$OPENVPN_PRIV_KEY", apiPayloadData.certRequest.privKey);
        } else if (protocol == configKey::awg) {
            // The server may either return a placeholder (client generates the key) or a real
            // client private key in last_config. Prefer the server-provided key when present,
            // otherwise fall back to the locally generated key we already sent in the request.
            QString effectiveClientPrivKey = apiPayloadData.wireGuardClientPrivKey;
            {
                const auto previewServerConfig = QJsonDocument::fromJson(configStr.toUtf8()).object();
                const auto previewContainers = previewServerConfig.value(config_key::containers).toArray();
                if (!previewContainers.isEmpty()) {
                    const auto previewContainerObject = previewContainers.at(0).toObject();
                    const auto previewContainerType =
                            ContainerProps::containerFromString(previewContainerObject.value(config_key::container).toString());
                    const QString previewContainerName = ContainerProps::containerTypeToString(previewContainerType);
                    const auto previewServerProtocolConfig = previewContainerObject.value(previewContainerName).toObject();
                    const auto previewClientProtocolConfig = QJsonDocument::fromJson(
                            previewServerProtocolConfig.value(config_key::last_config).toString().toUtf8()).object();
                    const QString previewPrivKey = previewClientProtocolConfig.value(config_key::client_priv_key).toString();
                    if (!previewPrivKey.isEmpty() && previewPrivKey != "$WIREGUARD_CLIENT_PRIVATE_KEY") {
                        effectiveClientPrivKey = previewPrivKey;
                        qDebug() << "[API IMPORT] using server-provided AWG private key";
                    }
                }
            }

            configStr.replace("$WIREGUARD_CLIENT_PRIVATE_KEY", effectiveClientPrivKey);
            auto newServerConfig = QJsonDocument::fromJson(configStr.toUtf8()).object();
            auto containers = newServerConfig.value(config_key::containers).toArray();
            if (containers.isEmpty()) {
                qDebug() << "missing containers field";
                return ErrorCode::ApiConfigEmptyError;
            }
            auto containerObject = containers.at(0).toObject();
            auto containerType = ContainerProps::containerFromString(containerObject.value(config_key::container).toString());
            // Use the exact container key from the API payload (e.g. "amnezia-awg"), not the
            // display alias returned by containerTypeToString (which is "awg" for AWG).
            QString containerName = containerObject.value(config_key::container).toString();
            auto serverProtocolConfig = containerObject.value(containerName).toObject();
            auto clientProtocolConfig =
                    QJsonDocument::fromJson(serverProtocolConfig.value(config_key::last_config).toString().toUtf8()).object();

            // Persist the actual private key inside last_config as well, so reconnects can reuse it.
            // The server returns client_private_key as a placeholder or omits it, but the public key
            // it stores is the one we sent in the API request. If we regenerate keys on every connect
            // the server peer changes and traffic stops flowing.
            const QString apiClientPrivKey = clientProtocolConfig.value(config_key::client_priv_key).toString();
            if (apiClientPrivKey == "$WIREGUARD_CLIENT_PRIVATE_KEY" || apiClientPrivKey.isEmpty()) {
                clientProtocolConfig[config_key::client_priv_key] = effectiveClientPrivKey;
                // Always derive the public key from the private key we persist. The payload public
                // key should already match, but if it ever drifts the server would store a key
                // that does not match the private key used for handshakes.
                clientProtocolConfig[config_key::client_pub_key] =
                        WireguardConfigurator::genPublicKeyFromPrivate(effectiveClientPrivKey);
                serverProtocolConfig[config_key::last_config] =
                        QString(QJsonDocument(clientProtocolConfig).toJson(QJsonDocument::Compact));
                qDebug() << "[API IMPORT] persisted AWG client key in last_config";
            }

            // Sync the core keys from last_config into the protocol config object so that
            // both JSON and INI consumers see the same values (especially client_priv_key).
            serverProtocolConfig[config_key::client_priv_key] = clientProtocolConfig.value(config_key::client_priv_key);
            serverProtocolConfig[config_key::client_pub_key] = clientProtocolConfig.value(config_key::client_pub_key);
            serverProtocolConfig[config_key::server_pub_key] = clientProtocolConfig.value(config_key::server_pub_key);
            serverProtocolConfig[config_key::psk_key] = clientProtocolConfig.value(config_key::psk_key);
            serverProtocolConfig[config_key::client_ip] = clientProtocolConfig.value(config_key::client_ip);
            serverProtocolConfig[config_key::mtu] = clientProtocolConfig.value(config_key::mtu);
            serverProtocolConfig[config_key::port] = clientProtocolConfig.value(config_key::port);
            serverProtocolConfig[config_key::hostName] = clientProtocolConfig.value(config_key::hostName);
            serverProtocolConfig[config_key::persistent_keep_alive] =
                    clientProtocolConfig.value(config_key::persistent_keep_alive).toString("25");

            // TODO looks like this block can be removed after v1 configs EOL

            serverProtocolConfig[config_key::junkPacketCount] = clientProtocolConfig.value(config_key::junkPacketCount);
            serverProtocolConfig[config_key::junkPacketMinSize] = clientProtocolConfig.value(config_key::junkPacketMinSize);
            serverProtocolConfig[config_key::junkPacketMaxSize] = clientProtocolConfig.value(config_key::junkPacketMaxSize);
            serverProtocolConfig[config_key::initPacketJunkSize] = clientProtocolConfig.value(config_key::initPacketJunkSize);
            serverProtocolConfig[config_key::responsePacketJunkSize] = clientProtocolConfig.value(config_key::responsePacketJunkSize);
            serverProtocolConfig[config_key::initPacketMagicHeader] = clientProtocolConfig.value(config_key::initPacketMagicHeader);
            serverProtocolConfig[config_key::responsePacketMagicHeader] = clientProtocolConfig.value(config_key::responsePacketMagicHeader);
            serverProtocolConfig[config_key::underloadPacketMagicHeader] = clientProtocolConfig.value(config_key::underloadPacketMagicHeader);
            serverProtocolConfig[config_key::transportPacketMagicHeader] = clientProtocolConfig.value(config_key::transportPacketMagicHeader);

            serverProtocolConfig[config_key::cookieReplyPacketJunkSize] = clientProtocolConfig.value(config_key::cookieReplyPacketJunkSize);
            serverProtocolConfig[config_key::transportPacketJunkSize] = clientProtocolConfig.value(config_key::transportPacketJunkSize);
            serverProtocolConfig[config_key::specialJunk1] = clientProtocolConfig.value(config_key::specialJunk1);
            serverProtocolConfig[config_key::specialJunk2] = clientProtocolConfig.value(config_key::specialJunk2);
            serverProtocolConfig[config_key::specialJunk3] = clientProtocolConfig.value(config_key::specialJunk3);
            serverProtocolConfig[config_key::specialJunk4] = clientProtocolConfig.value(config_key::specialJunk4);
            serverProtocolConfig[config_key::specialJunk5] = clientProtocolConfig.value(config_key::specialJunk5);

            // Ensure a wg-quick/AWG INI is always present in config.config. The iOS extension and the
            // settings screen treat this INI as the single source of truth. If the API payload already
            // contains a valid INI we keep it; otherwise synthesise one from the JSON last_config.
            {
                QString iniConfig = serverProtocolConfig.value(config_key::config).toString();
                const QRegularExpression privateKeyRe("PrivateKey\\s*=\\s*(\\S+)");
                const auto privateKeyMatch = privateKeyRe.match(iniConfig);
                const bool hasValidPrivateKey = privateKeyMatch.hasMatch()
                                                && !privateKeyMatch.captured(1).startsWith("$");
                if (!iniConfig.contains("[Interface]") || !hasValidPrivateKey) {
                    const QString hostName = newServerConfig.value(config_key::hostName).toString();
                    const QString port = clientProtocolConfig.value(config_key::port).toString(protocols::awg::defaultPort);
                    const QString dns1 = newServerConfig.value(config_key::dns1).toString();

                    QStringList lines;
                    lines << "[Interface]";
                    lines << QString("PrivateKey = %1").arg(clientProtocolConfig.value(config_key::client_priv_key).toString());
                    lines << QString("Address = %1").arg(clientProtocolConfig.value(config_key::client_ip).toString());
                    lines << QString("MTU = %1").arg(clientProtocolConfig.value(config_key::mtu).toString(protocols::awg::defaultMtu));
                    if (!dns1.isEmpty()) {
                        lines << QString("DNS = %1").arg(dns1);
                    }
                    lines << QString("Jc = %1").arg(clientProtocolConfig.value(config_key::junkPacketCount).toString());
                    lines << QString("Jmin = %1").arg(clientProtocolConfig.value(config_key::junkPacketMinSize).toString());
                    lines << QString("Jmax = %1").arg(clientProtocolConfig.value(config_key::junkPacketMaxSize).toString());
                    lines << QString("S1 = %1").arg(clientProtocolConfig.value(config_key::initPacketJunkSize).toString());
                    lines << QString("S2 = %1").arg(clientProtocolConfig.value(config_key::responsePacketJunkSize).toString());
                    lines << QString("S3 = %1").arg(clientProtocolConfig.value(config_key::cookieReplyPacketJunkSize).toString());
                    lines << QString("S4 = %1").arg(clientProtocolConfig.value(config_key::transportPacketJunkSize).toString());
                    lines << QString("H1 = %1").arg(clientProtocolConfig.value(config_key::initPacketMagicHeader).toString());
                    lines << QString("H2 = %1").arg(clientProtocolConfig.value(config_key::responsePacketMagicHeader).toString());
                    lines << QString("H3 = %1").arg(clientProtocolConfig.value(config_key::underloadPacketMagicHeader).toString());
                    lines << QString("H4 = %1").arg(clientProtocolConfig.value(config_key::transportPacketMagicHeader).toString());
                    lines << QString("I1 = %1").arg(clientProtocolConfig.value(config_key::specialJunk1).toString());
                    lines << QString("I2 = %1").arg(clientProtocolConfig.value(config_key::specialJunk2).toString());
                    lines << QString("I3 = %1").arg(clientProtocolConfig.value(config_key::specialJunk3).toString());
                    lines << QString("I4 = %1").arg(clientProtocolConfig.value(config_key::specialJunk4).toString());
                    lines << QString("I5 = %1").arg(clientProtocolConfig.value(config_key::specialJunk5).toString());
                    lines << "";
                    lines << "[Peer]";
                    lines << QString("PublicKey = %1").arg(clientProtocolConfig.value(config_key::server_pub_key).toString());
                    const QString psk = clientProtocolConfig.value(config_key::psk_key).toString();
                    if (!psk.isEmpty()) {
                        lines << QString("PresharedKey = %1").arg(psk);
                    }
                    lines << QString("Endpoint = %1:%2").arg(hostName, port);
                    lines << "AllowedIPs = 0.0.0.0/0, ::/0";
                    lines << QString("PersistentKeepalive = %1")
                                        .arg(clientProtocolConfig.value(config_key::persistent_keep_alive).toString("25"));

                    serverProtocolConfig[config_key::config] = lines.join("\n");
                    qDebug().noquote() << "[API IMPORT] generated AWG INI config:\n" << serverProtocolConfig.value(config_key::config).toString();
                }

                // Persist the INI inside last_config as well. The VPN configuration builder on iOS
                // reads containerConfig["awg"]["last_config"], parses it as JSON, and then uses the
                // "config" field as the wg-quick/AWG INI source of truth.
                clientProtocolConfig[config_key::config] = serverProtocolConfig.value(config_key::config).toString();
                serverProtocolConfig[config_key::last_config] =
                        QString(QJsonDocument(clientProtocolConfig).toJson(QJsonDocument::Compact));
            }

            //

            containerObject[containerName] = serverProtocolConfig;
            // The iOS connection path looks up the AWG protocol data by the short key "awg",
            // while API configs store the full config under the container key "amnezia-awg".
            // Keep both keys in sync so the VPN configuration builder finds a complete config.
            if (containerType == DockerContainer::Awg || containerType == DockerContainer::Awg2) {
                containerObject[configKey::awg] = serverProtocolConfig;
            }
            containers.replace(0, containerObject);
            newServerConfig[config_key::containers] = containers;
            configStr = QString(QJsonDocument(newServerConfig).toJson());
        }

        QJsonObject newServerConfig = QJsonDocument::fromJson(configStr.toUtf8()).object();
        serverConfig[config_key::dns1] = newServerConfig.value(config_key::dns1);
        serverConfig[config_key::dns2] = newServerConfig.value(config_key::dns2);
        serverConfig[config_key::containers] = newServerConfig.value(config_key::containers);
        serverConfig[config_key::hostName] = newServerConfig.value(config_key::hostName);

        if (newServerConfig.value(config_key::configVersion).toInt() == apiDefs::ConfigSource::AmneziaGateway) {
            serverConfig[config_key::configVersion] = newServerConfig.value(config_key::configVersion);
            // Never overwrite the user-visible name/description from the API response.
            // The API config payload contains generic names like "FRKN VLESS"; we keep
            // the names generated during import/reload (reelsoprovod <country> <protocol>).
            if (!serverConfig.contains(config_key::name)) {
                serverConfig[config_key::name] = newServerConfig.value(config_key::name);
            }
            if (!serverConfig.contains(config_key::description)) {
                serverConfig[config_key::description] = newServerConfig.value(config_key::description);
            }
        } else if (!serverConfig.contains(config_key::configVersion)) {
            serverConfig[config_key::configVersion] = apiDefs::ConfigSource::AmneziaGateway;
        }

        auto defaultContainer = newServerConfig.value(config_key::defaultContainer).toString();
        serverConfig[config_key::defaultContainer] = defaultContainer;

        qDebug().noquote() << "[API CONFIG] defaultContainer:" << defaultContainer;
        qDebug().noquote() << "[API CONFIG] containers:" << QJsonDocument(newServerConfig.value(config_key::containers).toArray()).toJson(QJsonDocument::Compact);

        QVariantMap map = serverConfig.value(configKey::apiConfig).toObject().toVariantMap();
        map.insert(newServerConfig.value(configKey::apiConfig).toObject().toVariantMap());
        auto apiConfig = QJsonObject::fromVariantMap(map);

        // Preserve auth_data passed in the outer serverConfig (subscription / import flows)
        // because the decrypted vpn key does not contain it.
        if (serverConfig.contains(configKey::authData)) {
            apiConfig.insert(configKey::authData, serverConfig.value(configKey::authData));
        }

        if (newServerConfig.value(config_key::configVersion).toInt() == apiDefs::ConfigSource::AmneziaGateway) {
            apiConfig.insert(apiDefs::key::supportedProtocols,
                             QJsonDocument::fromJson(apiResponseBody).object().value(apiDefs::key::supportedProtocols).toArray());

            apiConfig.insert(apiDefs::key::serviceInfo,
                             QJsonDocument::fromJson(apiResponseBody).object().value(apiDefs::key::serviceInfo).toObject());
        }

        serverConfig[configKey::apiConfig] = apiConfig;

        return ErrorCode::NoError;
    }
}

ApiConfigsController::ApiConfigsController(const QSharedPointer<ServersModel> &serversModel,
                                           const QSharedPointer<ApiServicesModel> &apiServicesModel,
                                           const std::shared_ptr<Settings> &settings, QObject *parent)
    : QObject(parent), m_serversModel(serversModel), m_apiServicesModel(apiServicesModel), m_settings(settings)
{
    m_selectedPlanIndex = defaultSubscriptionPlanIndex;
    for (const auto &product : subscriptionProducts) {
        QVariantMap plan;
        plan[QStringLiteral("productId")] = product.productId;
        plan[QStringLiteral("months")] = product.months;
        plan[QStringLiteral("price")] = QString();
        plan[QStringLiteral("currency")] = QString();
        m_subscriptionPlans.append(plan);
    }
}

QString ApiConfigsController::getSubscriptionId() const
{
    return resolveSubscriptionId();
}

QString ApiConfigsController::resolveSubscriptionId() const
{
    if (!m_subscriptionId.isEmpty()) {
        return m_subscriptionId;
    }
    // recover the subscription id from an already imported gateway server
    for (int i = 0; i < m_serversModel->getServersCount(); ++i) {
        const QJsonObject serverConfig = m_serversModel->getServerConfig(i);
        const QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
        if (apiConfig.value("connection_uuid").toString().isEmpty()) {
            continue;
        }
        QString subscriptionId = apiConfig.value(configKey::authData).toObject().value(apiDefs::key::apiKey).toString();
        if (subscriptionId.isEmpty()) {
            subscriptionId = serverConfig.value(configKey::authData).toObject().value(apiDefs::key::apiKey).toString();
        }
        if (!subscriptionId.isEmpty()) {
            return subscriptionId;
        }
    }
    return QString();
}

void ApiConfigsController::setSubscriptionId(const QString &subscriptionId)
{
    if (m_subscriptionId != subscriptionId) {
        m_subscriptionId = subscriptionId;
        emit subscriptionIdChanged();
    }
}

QString ApiConfigsController::getSelectedServerCountryCode() const
{
    return m_selectedServerCountryCode;
}

void ApiConfigsController::setSelectedServerCountryCode(const QString &countryCode)
{
    if (m_selectedServerCountryCode != countryCode) {
        m_selectedServerCountryCode = countryCode;
        emit selectedServerCountryCodeChanged();
    }
}

bool ApiConfigsController::getImportAllCountries() const
{
    return m_importAllCountries;
}

QVariantList ApiConfigsController::subscriptionPlans() const
{
    return m_subscriptionPlans;
}

int ApiConfigsController::selectedPlanIndex() const
{
    return m_selectedPlanIndex;
}

void ApiConfigsController::setSelectedPlanIndex(int index)
{
    if (index < 0 || index >= m_subscriptionPlans.size()) {
        return;
    }
    if (m_selectedPlanIndex != index) {
        m_selectedPlanIndex = index;

#if defined(Q_OS_IOS) || defined(MACOS_NE)
        const QVariantMap plan = m_subscriptionPlans.at(index).toMap();
        const QString price = plan.value(QStringLiteral("price")).toString();
        if (!price.isEmpty()) {
            QString formattedPrice = price;
            const QString currency = plan.value(QStringLiteral("currency")).toString();
            if (!currency.isEmpty()) {
                formattedPrice += " " + currency;
            }
            m_apiServicesModel->updateServicePrice(formattedPrice);
        }
#endif

        emit selectedPlanIndexChanged();
    }
}

void ApiConfigsController::setImportAllCountries(bool importAll)
{
    if (m_importAllCountries != importAll) {
        m_importAllCountries = importAll;
        emit importAllCountriesChanged();
    }
}

bool ApiConfigsController::createTrial(const QString &email, const QString &referralCode)
{
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, email, referralCode]() { createTrial(email, referralCode); }, Qt::QueuedConnection);
        return true;
    }

    QJsonObject body;
    body["trial"] = true;
    if (!email.isEmpty()) {
        body["email"] = email;
    }
    body["referred_by"] = referralCode.isEmpty() ? QString("WEB") : referralCode;
    body["language"] = m_settings->getAppLanguage().name().split("_").first();
    body["os"] = QSysInfo::productType();
    body["app_version"] = QString(APP_VERSION);
    body["installation_uuid"] = m_settings->getInstallationUuid(true);

    QNetworkRequest request;
    request.setUrl(QUrl("https://api.frkn.org/account"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);

    QNetworkReply *reply = amnApp->networkManager()->post(request, QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        QByteArray responseData = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[TRIAL] request failed:" << reply->errorString() << "response:" << responseData;
            emit errorOccurred(ErrorCode::InternalError);
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(responseData).object();
        QString id = response.value("subscription_id").toString();
        if (id.isEmpty()) {
            qWarning() << "[TRIAL] no subscription id in response:" << responseData;
            emit errorOccurred(ErrorCode::InternalError);
            return;
        }

        qDebug() << "[TRIAL] created subscription id:" << id;
        setSubscriptionId(id);
    });

    return true;
}

bool ApiConfigsController::exportVpnKey(const QString &fileName)
{
    if (fileName.isEmpty()) {
        emit errorOccurred(ErrorCode::PermissionsError);
        return false;
    }

    prepareVpnKeyExport();
    if (m_vpnKey.isEmpty()) {
        emit errorOccurred(ErrorCode::ApiConfigEmptyError);
        return false;
    }

    SystemController::saveFile(fileName, m_vpnKey);
    return true;
}

bool ApiConfigsController::exportNativeConfig(const QString &serverCountryCode, const QString &fileName)
{
    if (fileName.isEmpty()) {
        emit errorOccurred(ErrorCode::PermissionsError);
        return false;
    }

    auto serverConfigObject = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            apiConfigObject.value(configKey::userCountryCode).toString(),
                                            serverCountryCode,
                                            apiConfigObject.value(configKey::serviceType).toString(),
                                            configKey::awg, // apiConfigObject.value(configKey::serviceProtocol).toString(),
                                            serverConfigObject.value(configKey::authData).toObject() };

    QString protocol = gatewayRequestData.serviceProtocol;
    ProtocolData protocolData = generateProtocolData(protocol);

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    appendProtocolDataToApiPayload(gatewayRequestData.serviceProtocol, protocolData, apiPayload);
    bool isTestPurchase = apiConfigObject.value(apiDefs::key::isTestPurchase).toBool(false);
    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/native_config"), apiPayload, responseBody, isTestPurchase);
    if (errorCode != ErrorCode::NoError) {
        emit errorOccurred(errorCode);
        return false;
    }

    QJsonObject jsonConfig = QJsonDocument::fromJson(responseBody).object();
    QString nativeConfig = jsonConfig.value(configKey::config).toString();
    nativeConfig.replace("$WIREGUARD_CLIENT_PRIVATE_KEY", protocolData.wireGuardClientPrivKey);

    SystemController::saveFile(fileName, nativeConfig);
    return true;
}

bool ApiConfigsController::revokeNativeConfig(const QString &serverCountryCode)
{
    auto serverConfigObject = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            apiConfigObject.value(configKey::userCountryCode).toString(),
                                            serverCountryCode,
                                            apiConfigObject.value(configKey::serviceType).toString(),
                                            configKey::awg, // apiConfigObject.value(configKey::serviceProtocol).toString(),
                                            serverConfigObject.value(configKey::authData).toObject() };

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    bool isTestPurchase = apiConfigObject.value(apiDefs::key::isTestPurchase).toBool(false);
    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/revoke_native_config"), apiPayload, responseBody, isTestPurchase);
    if (errorCode != ErrorCode::NoError && errorCode != ErrorCode::ApiNotFoundError) {
        emit errorOccurred(errorCode);
        return false;
    }
    return true;
}

void ApiConfigsController::prepareVpnKeyExport()
{
    auto serverConfigObject = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    auto vpnKey = apiConfigObject.value(apiDefs::key::vpnKey).toString();
    if (vpnKey.isEmpty()) {
        vpnKey = apiUtils::getPremiumV2VpnKey(serverConfigObject);
        apiConfigObject.insert(apiDefs::key::vpnKey, vpnKey);
        serverConfigObject.insert(configKey::apiConfig, apiConfigObject);
        m_serversModel->editServer(serverConfigObject, m_serversModel->getProcessedServerIndex());
    }

    m_vpnKey = vpnKey;

    vpnKey.replace("vpn://", "");

    m_qrCodes = qrCodeUtils::generateQrCodeImageSeries(vpnKey.toUtf8());

    emit vpnKeyExportReady();
}

void ApiConfigsController::copyVpnKeyToClipboard()
{
    auto clipboard = amnApp->getClipboard();
    clipboard->setText(m_vpnKey);
}

void ApiConfigsController::copySubscriptionIdToClipboard()
{
    auto clipboard = amnApp->getClipboard();
    clipboard->setText(resolveSubscriptionId());
}

bool ApiConfigsController::fillAvailableServices()
{
    QJsonObject apiPayload;
    apiPayload[configKey::osVersion] = QSysInfo::productType();
    apiPayload[apiDefs::key::appLanguage] = m_settings->getAppLanguage().name().split("_").first();

    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/services"), apiPayload, responseBody);
    if (errorCode == ErrorCode::NoError) {
        if (!responseBody.contains("services")) {
            errorCode = ErrorCode::ApiServicesMissingError;
        }
    }

    if (errorCode != ErrorCode::NoError) {
        emit errorOccurred(errorCode);
        return false;
    }

    QJsonObject data = QJsonDocument::fromJson(responseBody).object();

    qDebug().noquote() << "[API SERVICES] response:" << QJsonDocument(data).toJson(QJsonDocument::Indented);

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    QStringList productIds;
    for (const auto &product : subscriptionProducts) {
        productIds << product.productId;
    }

    QEventLoop waitProducts;
    QList<QVariantMap> fetchedProducts;

    IosController::Instance()->fetchProducts(productIds,
                                             [&](const QList<QVariantMap> &products,
                                                 const QStringList &invalidIds,
                                                 const QString &errorString) {
                                                 if (!errorString.isEmpty() || products.isEmpty()) {
                                                     qWarning().noquote() << "[IAP] Failed to fetch product prices:" << errorString
                                                                          << "invalid ids:" << invalidIds;
                                                 } else {
                                                     fetchedProducts = products;
                                                 }
                                                 waitProducts.quit();
                                             });
    waitProducts.exec();

    QString defaultPlanPrice;
    QString defaultPlanCurrency;
    for (int i = 0; i < m_subscriptionPlans.size(); ++i) {
        QVariantMap plan = m_subscriptionPlans[i].toMap();
        for (const auto &product : fetchedProducts) {
            if (product.value(QStringLiteral("productId")).toString() == plan.value(QStringLiteral("productId")).toString()) {
                plan[QStringLiteral("price")] = product.value(QStringLiteral("price")).toString();
                plan[QStringLiteral("currency")] = product.value(QStringLiteral("currencyCode")).toString();
                m_subscriptionPlans[i] = plan;
                if (i == defaultSubscriptionPlanIndex) {
                    defaultPlanPrice = plan.value(QStringLiteral("price")).toString();
                    defaultPlanCurrency = plan.value(QStringLiteral("currency")).toString();
                }
                qInfo().noquote() << "[IAP] Fetched product price:" << plan.value(QStringLiteral("productId")).toString()
                                  << plan.value(QStringLiteral("price")).toString() << plan.value(QStringLiteral("currency")).toString();
                break;
            }
        }
    }
    emit subscriptionPlansChanged();

    if (!defaultPlanPrice.isEmpty()) {
        QJsonArray services = data.value("services").toArray();
        for (int i = 0; i < services.size(); ++i) {
            QJsonObject service = services[i].toObject();
            if (service.value(configKey::serviceType).toString() == serviceType::amneziaPremium) {
                QJsonObject serviceInfo = service.value(configKey::serviceInfo).toObject();
                QString formattedPrice = defaultPlanPrice;
                if (!defaultPlanCurrency.isEmpty()) {
                    formattedPrice += " " + defaultPlanCurrency;
                }
                serviceInfo["price"] = formattedPrice;
                service[configKey::serviceInfo] = serviceInfo;
                services[i] = service;
                data["services"] = services;
                qInfo().noquote() << "[IAP] Updated premium service price in data:" << formattedPrice;
                break;
            }
        }
    }
#endif
    
    m_apiServicesModel->updateModel(data);
    if (m_apiServicesModel->rowCount() > 0) {
        m_apiServicesModel->setServiceIndex(0);
    }
    return true;
}

bool ApiConfigsController::importService()
{
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    bool isIosOrMacOsNe = true;
#else
    bool isIosOrMacOsNe = false;
#endif

    if (m_apiServicesModel->getSelectedServiceType() == serviceType::amneziaPremium) {
        if (isIosOrMacOsNe) {
            importSerivceFromAppStore();
            return true;
        }
    } else {
        importServiceFromGateway();
        return true;
    }
    return false;
}

bool ApiConfigsController::importSerivceFromAppStore()
{
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    QString selectedProductId = QStringLiteral("frkn_premium_6_month");
    if (m_selectedPlanIndex >= 0 && m_selectedPlanIndex < m_subscriptionPlans.size()) {
        selectedProductId = m_subscriptionPlans[m_selectedPlanIndex].toMap().value(QStringLiteral("productId")).toString();
    }

    bool purchaseOk = false;
    QString originalTransactionId;
    QString storeTransactionId;
    QString storeProductId;
    QString purchaseError;
    QEventLoop waitPurchase;
    IosController::Instance()->purchaseProduct(selectedProductId,
                                               [&](bool success, const QString &txId, const QString &purchasedProductId,
                                                   const QString &originalTxId, const QString &errorString) {
                                                   purchaseOk = success;
                                                   originalTransactionId = originalTxId;
                                                   storeTransactionId = txId;
                                                   storeProductId = purchasedProductId;
                                                   purchaseError = errorString;
                                                   waitPurchase.quit();
                                               });
    waitPurchase.exec();

    if (!purchaseOk || originalTransactionId.isEmpty()) {
        qDebug() << "IAP purchase failed:" << purchaseError;
        emit errorOccurred(ErrorCode::ApiPurchaseError);
        return false;
    }
    qInfo().noquote() << "[IAP] Purchase success. transactionId =" << storeTransactionId
                      << "originalTransactionId =" << originalTransactionId << "productId =" << storeProductId;

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            m_apiServicesModel->getCountryCode(),
                                            "",
                                            m_apiServicesModel->getSelectedServiceType(),
                                            m_apiServicesModel->getSelectedServiceProtocol(),
                                            QJsonObject() };

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    apiPayload[apiDefs::key::transactionId] = originalTransactionId;
    apiPayload[configKey::productId] = storeProductId.isEmpty() ? selectedProductId : storeProductId;
    auto isTestPurchase = IosController::Instance()->isTestFlight();

    ErrorCode errorCode;
    QByteArray responseBody;
    errorCode = executeRequest(QString("%1v1/subscriptions"), apiPayload, responseBody, isTestPurchase);
    if (errorCode != ErrorCode::NoError) {
        emit errorOccurred(errorCode);
        return false;
    }

    errorCode = importServiceFromBilling(responseBody, isTestPurchase);
    if (errorCode != ErrorCode::NoError) {
        emit errorOccurred(errorCode);
        return false;
    }

    emit installServerFromApiFinished(tr("%1 installed successfully.").arg(m_apiServicesModel->getSelectedServiceName()));
#endif
    return true;
}

bool ApiConfigsController::restoreSerivceFromAppStore()
{
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    const QString premiumServiceType = QStringLiteral("amnezia-premium");

    if (!fillAvailableServices()) {
        qWarning().noquote() << "[IAP] Unable to fetch services list before restore";
        emit errorOccurred(ErrorCode::ApiServicesMissingError);
        return false;
    }

    if (m_apiServicesModel->rowCount() <= 0) {
        emit errorOccurred(ErrorCode::ApiServicesMissingError);
        return false;
    }

    // Ensure we have a valid premium selection for gateway requests
    bool premiumSelected = false;
    for (int i = 0; i < m_apiServicesModel->rowCount(); ++i) {
        m_apiServicesModel->setServiceIndex(i);
        if (m_apiServicesModel->getSelectedServiceType() == premiumServiceType) {
            premiumSelected = true;
            break;
        }
    }

    if (!premiumSelected) {
        emit errorOccurred(ErrorCode::ApiServicesMissingError);
        return false;
    }

    bool restoreSuccess = false;
    QList<QVariantMap> restoredTransactions;
    QString restoreError;
    QEventLoop waitRestore;

    IosController::Instance()->restorePurchases([&](bool success, const QList<QVariantMap> &transactions, const QString &errorString) {
        restoreSuccess = success;
        restoredTransactions = transactions;
        restoreError = errorString;
        waitRestore.quit();
    });
    waitRestore.exec();

    if (!restoreSuccess) {
        qWarning().noquote() << "[IAP] Restore failed:" << restoreError;
        emit errorOccurred(ErrorCode::ApiPurchaseError);
        return false;
    }

    if (restoredTransactions.isEmpty()) {
        qInfo().noquote() << "[IAP] Restore completed, but no transactions were returned";
        emit errorOccurred(ErrorCode::ApiPurchaseError);
        return false;
    }

    bool hasInstalledConfig = false;
    bool duplicateConfigAlreadyPresent = false;
    int duplicateCount = 0;
    QSet<QString> processedTransactions;
    for (const QVariantMap &transaction : restoredTransactions) {
        const QString originalTransactionId = transaction.value(QStringLiteral("originalTransactionId")).toString();
        const QString transactionId = transaction.value(QStringLiteral("transactionId")).toString();
        const QString productId = transaction.value(QStringLiteral("productId")).toString();

        if (originalTransactionId.isEmpty()) {
            qWarning().noquote() << "[IAP] Skipping restored transaction without originalTransactionId" << transactionId;
            continue;
        }

        if (processedTransactions.contains(originalTransactionId)) {
            duplicateCount++;
            continue;
        }
        processedTransactions.insert(originalTransactionId);

        qInfo().noquote() << "[IAP] Restoring subscription. transactionId =" << transactionId
                          << "originalTransactionId =" << originalTransactionId << "productId =" << productId;

        GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                                QString(APP_VERSION),
                                                m_settings->getAppLanguage().name().split("_").first(),
                                                m_settings->getInstallationUuid(true),
                                                m_apiServicesModel->getCountryCode(),
                                                "",
                                                m_apiServicesModel->getSelectedServiceType(),
                                                m_apiServicesModel->getSelectedServiceProtocol(),
                                                QJsonObject() };

        QJsonObject apiPayload = gatewayRequestData.toJsonObject();
        apiPayload[apiDefs::key::transactionId] = originalTransactionId;
        if (!productId.isEmpty()) {
            apiPayload[configKey::productId] = productId;
        }
        auto isTestPurchase = IosController::Instance()->isTestFlight();
        QByteArray responseBody;
        ErrorCode errorCode = executeRequest(QString("%1v1/subscriptions"), apiPayload, responseBody, isTestPurchase);
        if (errorCode != ErrorCode::NoError) {
            qWarning().noquote() << "[IAP] Failed to restore transaction" << originalTransactionId
                                 << "errorCode =" << static_cast<int>(errorCode);
            continue;
        }

        ErrorCode installError = importServiceFromBilling(responseBody, isTestPurchase);
        if (errorCode == ErrorCode::ApiConfigAlreadyAdded) {
            duplicateConfigAlreadyPresent = true;
            qInfo().noquote() << "[IAP] Skipping restored transaction" << originalTransactionId
                              << "because subscription config with the same vpn_key already exists";
        } else if (errorCode != ErrorCode::NoError) {
            qWarning().noquote() << "[IAP] Failed to process restored subscription response for transaction" << originalTransactionId;
        } else {
            hasInstalledConfig = true;
        }
    }

    if (!hasInstalledConfig) {
        const ErrorCode restoreError = duplicateConfigAlreadyPresent ? ErrorCode::ApiConfigAlreadyAdded : ErrorCode::ApiPurchaseError;
        emit errorOccurred(restoreError);
        return false;
    }

    emit installServerFromApiFinished(tr("Subscription restored successfully."));
    if (duplicateCount > 0) {
        qInfo().noquote() << "[IAP] Skipped" << duplicateCount
                          << "duplicate restored transactions for original transaction IDs already processed";
    }
#endif
    return true;
}

bool ApiConfigsController::importServiceForCountry(const QString &serverCountryCode, const ProtocolData &protocolData)
{
    QJsonObject authData;
    authData[apiDefs::key::apiKey] = m_subscriptionId.isEmpty() ? m_settings->getInstallationUuid(true) : m_subscriptionId;
    authData[apiDefs::key::id] = m_subscriptionId.isEmpty() ? m_settings->getInstallationUuid(true) : m_subscriptionId;

    QString userCountryCode = m_apiServicesModel->getCountryCode();

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            userCountryCode,
                                            serverCountryCode,
                                            m_apiServicesModel->getSelectedServiceType(),
                                            m_apiServicesModel->getSelectedServiceProtocol(),
                                            authData };

    qDebug() << "[API IMPORT] selected service protocol:" << gatewayRequestData.serviceProtocol
             << "userCountryCode:" << userCountryCode << "serverCountryCode:" << serverCountryCode;

    if (m_serversModel->isServerFromApiAlreadyExists(serverCountryCode, gatewayRequestData.serviceType,
                                                     gatewayRequestData.serviceProtocol)) {
        qDebug() << "[API IMPORT] server already exists for" << serverCountryCode;
        return true;
    }

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    appendProtocolDataToApiPayload(gatewayRequestData.serviceProtocol, protocolData, apiPayload);

    ErrorCode errorCode;
    QByteArray responseBody;

    errorCode = executeRequest(QString("%1v1/config"), apiPayload, responseBody);

    QJsonObject serverConfig;
    if (errorCode == ErrorCode::NoError) {
        errorCode = fillServerConfig(gatewayRequestData.serviceProtocol, protocolData, responseBody, serverConfig);
        if (errorCode != ErrorCode::NoError) {
            qWarning() << "[API IMPORT] failed to fill config for" << serverCountryCode << "error:" << static_cast<int>(errorCode);
            return false;
        }

        // fillServerConfig may have lost auth_data when the decrypted config didn't include it;
        // restore it from the request payload before saving.
        serverConfig.insert(configKey::authData, authData);

        QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
        apiConfig.insert(configKey::userCountryCode, serverCountryCode);
        apiConfig.insert(configKey::serviceType, m_apiServicesModel->getSelectedServiceType());
        // Prefer the protocol reported by the gateway for this specific config;
        // the service card protocol ("vless" for the merged premium card) is only a fallback.
        if (apiConfig.value(configKey::serviceProtocol).toString().isEmpty()) {
            apiConfig.insert(configKey::serviceProtocol, m_apiServicesModel->getSelectedServiceProtocol());
        }
        apiConfig.insert(configKey::authData, authData);

        serverConfig.insert(configKey::apiConfig, apiConfig);
        serverConfig.insert(configKey::authData, authData);

        QString hostName = serverConfig.value(config_key::hostName).toString();
        QString protocolName = apiConfig.value(configKey::serviceProtocol).toString(gatewayRequestData.serviceProtocol).toUpper();
        QString name = QString("reelsoprovod %1 %2").arg(serverCountryCode.toUpper(), protocolName);
        QString description = QString("%1 %2").arg(protocolName, hostName);

        serverConfig[config_key::name] = name;
        serverConfig[config_key::description] = description;

        QJsonObject displayInfo;
        displayInfo["countryCode"] = serverCountryCode.toUpper();
        displayInfo["countryName"] = serverCountryCode.toUpper();
        displayInfo["protocol"] = protocolName;
        displayInfo["hostName"] = hostName;
        displayInfo["serviceName"] = m_apiServicesModel->getSelectedServiceName();
        serverConfig["displayInfo"] = displayInfo;

        m_serversModel->addServer(serverConfig);
        return true;
    } else {
        qWarning() << "[API IMPORT] request failed for" << serverCountryCode << "error:" << static_cast<int>(errorCode);
        return false;
    }
}

bool ApiConfigsController::importServiceFromGateway()
{
    ProtocolData protocolData = generateProtocolData(m_apiServicesModel->getSelectedServiceProtocol());

    QString userCountryCode = m_apiServicesModel->getCountryCode();
    QString serverCountryCode = m_selectedServerCountryCode;

    if (m_importAllCountries) {
        auto availableCountries = m_apiServicesModel->getSelectedServiceCountries();
        bool anySuccess = false;
        for (const auto &country : availableCountries) {
            auto countryCode = country.toObject().value(configKey::countryCode).toString();
            if (importServiceForCountry(countryCode, protocolData)) {
                anySuccess = true;
            }
        }

        if (anySuccess) {
            emit installServerFromApiFinished(tr("%1 installed successfully.").arg(m_apiServicesModel->getSelectedServiceName()));
            return true;
        } else {
            emit errorOccurred(ErrorCode::ApiConfigEmptyError);
            return false;
        }
    }

    if (serverCountryCode.isEmpty() && m_apiServicesModel->getSelectedServiceProtocol() == configKey::awg) {
        auto availableCountries = m_apiServicesModel->getSelectedServiceCountries();
        bool countryAvailable = false;
        for (const auto &country : availableCountries) {
            auto countryCode = country.toObject().value(configKey::countryCode).toString();
            if (countryCode == userCountryCode) {
                countryAvailable = true;
                break;
            }
        }
        if (!countryAvailable && !availableCountries.isEmpty()) {
            serverCountryCode = availableCountries.first().toObject().value(configKey::countryCode).toString();
            qDebug() << "[API IMPORT] awg not available in" << userCountryCode << ", using" << serverCountryCode;
        }
    }

    if (importServiceForCountry(serverCountryCode, protocolData)) {
        emit installServerFromApiFinished(tr("%1 installed successfully.").arg(m_apiServicesModel->getSelectedServiceName()));
        return true;
    } else {
        emit errorOccurred(ErrorCode::ApiConfigEmptyError);
        return false;
    }
}

bool ApiConfigsController::updateServiceFromGateway(const int serverIndex, const QString &newCountryCode, const QString &newCountryName,
                                                    bool reloadServiceConfig, bool silent)
{
    auto serverConfig = m_serversModel->getServerConfig(serverIndex);
    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();

    qDebug().noquote() << "[UPDATE GATEWAY] serverIndex:" << serverIndex
                       << "configVersion:" << serverConfig.value("config_version").toInt()
                       << "apiConfig keys:" << apiConfig.keys()
                       << "authData keys:" << serverConfig.value(configKey::authData).toObject().keys();

    const bool isConnectEvent = newCountryCode.isEmpty() && newCountryName.isEmpty() && !reloadServiceConfig;

    QJsonObject authData = apiConfig.value(configKey::authData).toObject();
    if (authData.isEmpty()) {
        authData = serverConfig.value(configKey::authData).toObject();
    }
    // Ensure we always send api_key for the new AGW endpoints.
    if (!authData.contains(apiDefs::key::apiKey) && authData.contains(apiDefs::key::id)) {
        authData[apiDefs::key::apiKey] = authData.value(apiDefs::key::id);
    }

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            apiConfig.value(configKey::userCountryCode).toString(),
                                            newCountryCode,
                                            apiConfig.value(configKey::serviceType).toString(),
                                            apiConfig.value(configKey::serviceProtocol).toString(),
                                            authData };

    ProtocolData protocolData = generateProtocolData(gatewayRequestData.serviceProtocol);

    // Re-use the previously generated WireGuard key pair on every reconnect so that
    // the server peer entry (which is keyed by the public key) stays the same.
    // Otherwise each connection gets a brand new key, the server creates a new peer
    // for the same client IP and the handshake is dropped or races with the old one.
    if (isConnectEvent && gatewayRequestData.serviceProtocol == configKey::awg) {
        const auto containers = serverConfig.value(config_key::containers).toArray();
        const QString containerName = ContainerProps::containerTypeToString(DockerContainer::Awg);
        for (const QJsonValue &containerValue : containers) {
            const auto awgProtocolConfig = containerValue.toObject().value(containerName).toObject();
            if (awgProtocolConfig.isEmpty()) {
                continue;
            }
            const auto lastConfig = QJsonDocument::fromJson(awgProtocolConfig.value(config_key::last_config).toString().toUtf8()).object();
            QString savedClientPrivKey = lastConfig.value(config_key::client_priv_key).toString();
            QString savedClientPubKey = lastConfig.value(config_key::client_pub_key).toString();

            // The public key can always be derived from the private key, so a stored
            // private key alone is enough to keep the same server-side peer on reconnect.
            // AGW configs often persist only client_priv_key, which previously blocked reuse
            // and caused a fresh key (and a new server peer for the same client IP) every time.
            if (!savedClientPrivKey.isEmpty()) {
                const QString derivedPubKey = WireguardConfigurator::genPublicKeyFromPrivate(savedClientPrivKey);
                if (savedClientPubKey.isEmpty()) {
                    savedClientPubKey = derivedPubKey;
                } else if (savedClientPubKey != derivedPubKey) {
                    qWarning() << "[API IMPORT] saved AWG public key does not match private key, deriving correct one"
                                << "serverIndex:" << serverIndex;
                    savedClientPubKey = derivedPubKey;
                }
            }

            if (!savedClientPrivKey.isEmpty() && !savedClientPubKey.isEmpty()) {
                protocolData.wireGuardClientPrivKey = savedClientPrivKey;
                protocolData.wireGuardClientPubKey = savedClientPubKey;
                qDebug() << "[API IMPORT] reusing existing AWG client key for connect event, serverIndex:" << serverIndex;
            } else {
                qDebug() << "[API IMPORT] no saved AWG client key to reuse, generating new one, serverIndex:" << serverIndex;
            }
            break;
        }
    }

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    // Pin the exact node on refresh/reconnect: without connection_id the backend picks
    // any node matching (protocol, country) and two same-country servers end up with
    // each other's configs (server card shows one IP, the stored config has another).
    // Not sent on explicit country change — there a different node is the whole point.
    const QString connectionUuid = apiConfig.value(QStringLiteral("connection_uuid")).toString();
    if (!connectionUuid.isEmpty() && newCountryCode.isEmpty()) {
        apiPayload.insert(QStringLiteral("connection_id"), connectionUuid);
    }
    // node_id (gateway v0.6.19+): connection_uuid alone is shared by all nodes of one
    // (env, protocol) group — pin the exact node. Survives refreshes via the apiConfig merge.
    const QString nodeId = apiConfig.value(QStringLiteral("node_id")).toString();
    if (!nodeId.isEmpty() && newCountryCode.isEmpty()) {
        apiPayload.insert(QStringLiteral("node_id"), nodeId);
    }
    appendProtocolDataToApiPayload(gatewayRequestData.serviceProtocol, protocolData, apiPayload);

    if (isConnectEvent) {
        apiPayload.insert(configKey::isConnectEvent, true);
    }

    bool isTestPurchase = apiConfig.value(apiDefs::key::isTestPurchase).toBool(false);
    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/config"), apiPayload, responseBody, isTestPurchase);

    QJsonObject newServerConfig;
    if (errorCode == ErrorCode::NoError) {
        errorCode = fillServerConfig(gatewayRequestData.serviceProtocol, protocolData, responseBody, newServerConfig);
        if (errorCode != ErrorCode::NoError) {
            if (!silent) {
                emit errorOccurred(errorCode);
            }
            return false;
        }

        // Preserve existing apiConfig keys (env, connection_uuid, service_info, ...) —
        // the gateway config payload doesn't carry them, and losing e.g. env removes
        // the server from its environment filter.
        QJsonObject newApiConfig = apiConfig;
        const QJsonObject fetchedApiConfig = newServerConfig.value(configKey::apiConfig).toObject();
        for (auto it = fetchedApiConfig.constBegin(); it != fetchedApiConfig.constEnd(); ++it) {
            newApiConfig.insert(it.key(), it.value());
        }
        newApiConfig.insert(configKey::userCountryCode, apiConfig.value(configKey::userCountryCode));
        newApiConfig.insert(configKey::serviceType, apiConfig.value(configKey::serviceType));
        newApiConfig.insert(configKey::serviceProtocol, apiConfig.value(configKey::serviceProtocol));
        newApiConfig.insert(apiDefs::key::vpnKey, apiConfig.value(apiDefs::key::vpnKey));

        newServerConfig.insert(configKey::apiConfig, newApiConfig);
        newServerConfig.insert(configKey::authData, gatewayRequestData.authData);
        newApiConfig.insert(configKey::authData, gatewayRequestData.authData);
        newServerConfig.insert(configKey::apiConfig, newApiConfig);
        newServerConfig.insert(config_key::crc, serverConfig.value(config_key::crc));

        if (serverConfig.value(config_key::nameOverriddenByUser).toBool()) {
            newServerConfig.insert(config_key::name, serverConfig.value(config_key::name));
            newServerConfig.insert(config_key::nameOverriddenByUser, true);
        } else {
            // Keep the existing generated name (reelsoprovod <country> <protocol>) instead of
            // overwriting it with the generic name returned by the API config endpoint.
            // fillServerConfig already preserves name/description unless they were missing.
            newServerConfig.insert(config_key::name, serverConfig.value(config_key::name));
            newServerConfig.insert(config_key::description, serverConfig.value(config_key::description));
        }

        // Preserve displayInfo so the UI can keep the country/protocol label after reload.
        if (serverConfig.contains("displayInfo")) {
            newServerConfig.insert("displayInfo", serverConfig.value("displayInfo"));
        }

        // Guard against a poisoned gateway response (e.g. backend returning an empty
        // config when the subscription is expired): never overwrite a good local
        // config with one that has no hostname or no containers.
        const auto containers = newServerConfig.value(config_key::containers).toArray();
        if (newServerConfig.value(config_key::hostName).toString().isEmpty() || containers.isEmpty()) {
            qWarning() << "ApiConfigsController::updateServiceFromGateway: refusing to save an empty"
                          "server config received from the gateway (hostName or containers missing),"
                          "keeping the local one";
            if (!silent) {
                emit errorOccurred(ErrorCode::ApiConfigDownloadError);
            }
            return false;
        }

        m_serversModel->editServer(newServerConfig, serverIndex);
        if (reloadServiceConfig) {
            emit reloadServerFromApiFinished(tr("API config reloaded"));
        } else if (newCountryName.isEmpty()) {
            emit updateServerFromApiFinished();
        } else {
            emit changeApiCountryFinished(tr("Successfully changed the country of connection to %1").arg(newCountryName));
        }
        return true;
    } else {
        if (!silent) {
            emit errorOccurred(errorCode);
        }
        return false;
    }
}

void ApiConfigsController::refreshSubscriptionConfigs()
{
    // Throttle: at most once per 6 hours — this runs on every app start.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now - m_settings->lastSubscriptionRefresh() < 6 * 60 * 60) {
        return;
    }
    m_settings->setLastSubscriptionRefresh(now);

    m_pendingSubscriptionRefresh.clear();
    const int serversCount = m_serversModel->getServersCount();
    for (int i = 0; i < serversCount; ++i) {
        const auto apiConfig = m_serversModel->getServerConfig(i).value(configKey::apiConfig).toObject();
        // only gateway-issued configs can be refreshed; manual/self-hosted ones are skipped
        if (!apiConfig.value("connection_uuid").toString().isEmpty()) {
            m_pendingSubscriptionRefresh.append(i);
        }
    }
    if (m_pendingSubscriptionRefresh.isEmpty()) {
        return;
    }

    qDebug() << "[SUBSCRIPTION] refreshing" << m_pendingSubscriptionRefresh.size() << "server config(s) from the gateway";
    processNextSubscriptionRefresh();
}

void ApiConfigsController::processNextSubscriptionRefresh()
{
    if (m_pendingSubscriptionRefresh.isEmpty()) {
        return;
    }
    const int serverIndex = m_pendingSubscriptionRefresh.takeFirst();
    // updateServiceFromGateway is synchronous but spins its own event loop, so the UI
    // stays responsive; servers are refreshed one-by-one, silent failures keep the local config
    updateServiceFromGateway(serverIndex, "", "", false, true);
    QTimer::singleShot(0, this, [this]() { processNextSubscriptionRefresh(); });
}

bool ApiConfigsController::updateServiceFromTelegram(const int serverIndex)
{
#ifdef Q_OS_IOS
    IosController::Instance()->requestInetAccess();
    QThread::msleep(10);
#endif

    GatewayController gatewayController(m_settings->getGatewayEndpoint(), m_settings->isDevGatewayEnv(), apiDefs::requestTimeoutMsecs,
                                        m_settings->isStrictKillSwitchEnabled());

    auto serverConfig = m_serversModel->getServerConfig(serverIndex);
    auto installationUuid = m_settings->getInstallationUuid(true);

    QString serviceProtocol = serverConfig.value(configKey::protocol).toString();
    ProtocolData protocolData = generateProtocolData(serviceProtocol);

    QJsonObject apiPayload;
    appendProtocolDataToApiPayload(serviceProtocol, protocolData, apiPayload);
    apiPayload[configKey::uuid] = installationUuid;
    apiPayload[configKey::osVersion] = QSysInfo::productType();
    apiPayload[configKey::appVersion] = QString(APP_VERSION);
    apiPayload[configKey::accessToken] = serverConfig.value(configKey::accessToken).toString();
    apiPayload[configKey::apiEndpoint] = serverConfig.value(configKey::apiEndpoint).toString();

    QByteArray responseBody;
    ErrorCode errorCode = gatewayController.post(QString("%1v1/proxy_config"), apiPayload, responseBody);

    if (errorCode == ErrorCode::NoError) {
        errorCode = fillServerConfig(serviceProtocol, protocolData, responseBody, serverConfig);
        if (errorCode != ErrorCode::NoError) {
            emit errorOccurred(errorCode);
            return false;
        }

        m_serversModel->editServer(serverConfig, serverIndex);
        emit updateServerFromApiFinished();
        return true;
    } else {
        emit errorOccurred(errorCode);
        return false;
    }
}

bool ApiConfigsController::deactivateDevice(const bool isRemoveEvent)
{
    auto serverIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfigObject = m_serversModel->getServerConfig(serverIndex);
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    if (!apiUtils::isPremiumServer(serverConfigObject)) {
        return true;
    }

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            apiConfigObject.value(configKey::userCountryCode).toString(),
                                            apiConfigObject.value(configKey::serverCountryCode).toString(),
                                            apiConfigObject.value(configKey::serviceType).toString(),
                                            "",
                                            serverConfigObject.value(configKey::authData).toObject() };

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    bool isTestPurchase = apiConfigObject.value(apiDefs::key::isTestPurchase).toBool(false);
    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/revoke_config"), apiPayload, responseBody, isTestPurchase);

    if (errorCode != ErrorCode::NoError && errorCode != ErrorCode::ApiNotFoundError) {
        emit errorOccurred(errorCode);
        return false;
    }

    serverConfigObject.remove(config_key::containers);
    m_serversModel->editServer(serverConfigObject, serverIndex);

    return true;
}

bool ApiConfigsController::deactivateExternalDevice(const QString &uuid, const QString &serverCountryCode)
{
    auto serverIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfigObject = m_serversModel->getServerConfig(serverIndex);
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    if (!apiUtils::isPremiumServer(serverConfigObject)) {
        return true;
    }

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            uuid,
                                            apiConfigObject.value(configKey::userCountryCode).toString(),
                                            serverCountryCode,
                                            apiConfigObject.value(configKey::serviceType).toString(),
                                            "",
                                            serverConfigObject.value(configKey::authData).toObject() };

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    bool isTestPurchase = apiConfigObject.value(apiDefs::key::isTestPurchase).toBool(false);
    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/revoke_config"), apiPayload, responseBody, isTestPurchase);
    if (errorCode != ErrorCode::NoError && errorCode != ErrorCode::ApiNotFoundError) {
        emit errorOccurred(errorCode);
        return false;
    }

    if (uuid == m_settings->getInstallationUuid(true)) {
        serverConfigObject.remove(config_key::containers);
        m_serversModel->editServer(serverConfigObject, serverIndex);
    }

    return true;
}

bool ApiConfigsController::isConfigValid()
{
    int serverIndex = m_serversModel->getDefaultServerIndex();
    QJsonObject serverConfigObject = m_serversModel->getServerConfig(serverIndex);
    auto configSource = apiUtils::getConfigSource(serverConfigObject);

    qDebug().noquote() << "[IS CONFIG VALID] serverIndex:" << serverIndex
                       << "configSource:" << static_cast<int>(configSource)
                       << "configVersion:" << serverConfigObject.value("config_version").toInt()
                       << "hasInstalledContainers:" << m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()
                       << "apiConfig keys:" << serverConfigObject.value(configKey::apiConfig).toObject().keys()
                       << "authData keys:" << serverConfigObject.value(configKey::authData).toObject().keys();

    if (configSource == apiDefs::ConfigSource::Telegram
        && !m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()) {
        m_serversModel->removeApiConfig(serverIndex);
        return updateServiceFromTelegram(serverIndex);
    } else if (configSource == apiDefs::ConfigSource::AmneziaGateway
               && !m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()) {
        qDebug() << "[IS CONFIG VALID] updating gateway config";
        return updateServiceFromGateway(serverIndex, "", "");
    } else if (configSource && m_serversModel->isApiKeyExpired(serverIndex)) {
        qDebug() << "[IS CONFIG VALID] updating by expires_at event";
        if (configSource == apiDefs::ConfigSource::AmneziaGateway) {
            const bool hasInstalledConfig = m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool();
            const bool updated = updateServiceFromGateway(serverIndex, "", "", false, hasInstalledConfig);
            if (!updated && hasInstalledConfig) {
                // the API is unreachable but a usable config is already installed —
                // connect with it instead of showing an error
                qWarning() << "[IS CONFIG VALID] config refresh failed, falling back to the installed config";
                return true;
            }
            return updated;
        } else {
            m_serversModel->removeApiConfig(serverIndex);
            return updateServiceFromTelegram(serverIndex);
        }
    }
    return true;
}

void ApiConfigsController::setCurrentProtocol(const QString &protocolName)
{
    auto serverIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfigObject = m_serversModel->getServerConfig(serverIndex);
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    apiConfigObject[configKey::serviceProtocol] = protocolName;

    serverConfigObject.insert(configKey::apiConfig, apiConfigObject);

    m_serversModel->editServer(serverConfigObject, serverIndex);
}

bool ApiConfigsController::isVlessProtocol()
{
    auto serverIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfigObject = m_serversModel->getServerConfig(serverIndex);
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    if (apiConfigObject[configKey::serviceProtocol].toString() == "vless") {
        return true;
    }
    return false;
}

bool ApiConfigsController::isAwgProtocol()
{
    auto serverIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfigObject = m_serversModel->getServerConfig(serverIndex);
    auto apiConfigObject = serverConfigObject.value(configKey::apiConfig).toObject();

    return apiConfigObject[configKey::serviceProtocol].toString() == "awg";
}

QString ApiConfigsController::getCurrentServerConfigJson()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    return QString(QJsonDocument(serverConfig).toJson(QJsonDocument::Indented));
}

QString ApiConfigsController::getCurrentServerConfigIni()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto containers = serverConfig.value(config_key::containers).toArray();
    if (containers.isEmpty()) {
        return "";
    }

    auto containerObj = containers.at(0).toObject();
    auto containerType = ContainerProps::containerFromString(containerObj.value(config_key::container).toString());
    // Use the exact container key from the config (e.g. "amnezia-awg"), not the
    // display alias from containerTypeToString ("awg") — the JSON is keyed by it.
    QString containerName = containerObj.value(config_key::container).toString();
    auto protocolConfig = containerObj.value(containerName).toObject();

    // For AWG/WireGuard the raw wg-quick INI is stored in the 'config' field.
    QString iniConfig = protocolConfig.value(config_key::config).toString();
    const QRegularExpression privateKeyRe("PrivateKey\\s*=\\s*(\\S+)");
    const auto privateKeyMatch = privateKeyRe.match(iniConfig);
    const bool hasValidPrivateKey = privateKeyMatch.hasMatch() && !privateKeyMatch.captured(1).startsWith("$");
    if (!iniConfig.isEmpty() && hasValidPrivateKey) {
        return iniConfig;
    }

    // Fallback: build an INI from the JSON last_config fields so the UI always has something to show.
    auto lastConfig = QJsonDocument::fromJson(protocolConfig.value(config_key::last_config).toString().toUtf8()).object();
    if (lastConfig.isEmpty()) {
        return "";
    }

    QStringList lines;
    lines << "[Interface]";
    lines << QString("PrivateKey = %1").arg(lastConfig.value(config_key::client_priv_key).toString());
    lines << QString("Address = %1").arg(lastConfig.value(config_key::client_ip).toString());
    lines << QString("MTU = %1").arg(lastConfig.value(config_key::mtu).toString());
    lines << QString("DNS = %1").arg(serverConfig.value(config_key::dns1).toString());

    if (containerType == DockerContainer::Awg || containerType == DockerContainer::Awg2) {
        lines << QString("Jc = %1").arg(lastConfig.value(config_key::junkPacketCount).toString());
        lines << QString("Jmin = %1").arg(lastConfig.value(config_key::junkPacketMinSize).toString());
        lines << QString("Jmax = %1").arg(lastConfig.value(config_key::junkPacketMaxSize).toString());
        lines << QString("S1 = %1").arg(lastConfig.value(config_key::initPacketJunkSize).toString());
        lines << QString("S2 = %1").arg(lastConfig.value(config_key::responsePacketJunkSize).toString());
        lines << QString("S3 = %1").arg(lastConfig.value(config_key::cookieReplyPacketJunkSize).toString());
        lines << QString("S4 = %1").arg(lastConfig.value(config_key::transportPacketJunkSize).toString());
        lines << QString("H1 = %1").arg(lastConfig.value(config_key::initPacketMagicHeader).toString());
        lines << QString("H2 = %1").arg(lastConfig.value(config_key::responsePacketMagicHeader).toString());
        lines << QString("H3 = %1").arg(lastConfig.value(config_key::underloadPacketMagicHeader).toString());
        lines << QString("H4 = %1").arg(lastConfig.value(config_key::transportPacketMagicHeader).toString());
        lines << QString("I1 = %1").arg(lastConfig.value(config_key::specialJunk1).toString());
        lines << QString("I2 = %1").arg(lastConfig.value(config_key::specialJunk2).toString());
        lines << QString("I3 = %1").arg(lastConfig.value(config_key::specialJunk3).toString());
        lines << QString("I4 = %1").arg(lastConfig.value(config_key::specialJunk4).toString());
        lines << QString("I5 = %1").arg(lastConfig.value(config_key::specialJunk5).toString());
    }

    lines << "";
    lines << "[Peer]";
    lines << QString("PublicKey = %1").arg(lastConfig.value(config_key::server_pub_key).toString());
    if (!lastConfig.value(config_key::psk_key).toString().isEmpty()) {
        lines << QString("PresharedKey = %1").arg(lastConfig.value(config_key::psk_key).toString());
    }

    QString hostName = serverConfig.value(config_key::hostName).toString();
    if (hostName.isEmpty()) {
        hostName = lastConfig.value(config_key::hostName).toString();
    }
    QString port = protocolConfig.value(config_key::port).toString();
    if (port.isEmpty()) {
        port = lastConfig.value(config_key::port).toString();
    }
    if (port.isEmpty()) {
        port = (containerType == DockerContainer::Awg || containerType == DockerContainer::Awg2)
                ? protocols::awg::defaultPort : protocols::wireguard::defaultPort;
    }
    lines << QString("Endpoint = %1:%2").arg(hostName, port);

    lines << "AllowedIPs = 0.0.0.0/0, ::/0";
    lines << QString("PersistentKeepalive = %1").arg(lastConfig.value(config_key::persistent_keep_alive).toString("25"));

    return lines.join("\n");
}

QString ApiConfigsController::getCurrentServerTunnelParams()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();
    auto containers = serverConfig.value(config_key::containers).toArray();

    QJsonObject result;
    result["name"] = serverConfig.value(config_key::name);
    result["hostName"] = serverConfig.value(config_key::hostName);
    result["defaultContainer"] = serverConfig.value(config_key::defaultContainer);
    result["serviceProtocol"] = apiConfig.value(configKey::serviceProtocol);
    result["userCountryCode"] = apiConfig.value(configKey::userCountryCode);
    result["serverCountryCode"] = apiConfig.value(configKey::serverCountryCode);
    result["dns1"] = serverConfig.value(config_key::dns1);
    result["dns2"] = serverConfig.value(config_key::dns2);

    if (!containers.isEmpty()) {
        auto containerObj = containers.at(0).toObject();
        auto containerType = ContainerProps::containerFromString(containerObj.value(config_key::container).toString());
        QString containerName = ContainerProps::containerTypeToString(containerType);
        auto protocolConfig = containerObj.value(containerName).toObject();
        auto lastConfig = QJsonDocument::fromJson(protocolConfig.value(config_key::last_config).toString().toUtf8()).object();

        result["mtu"] = lastConfig.value(config_key::mtu);
        result["client_ip"] = lastConfig.value(config_key::client_ip);

        QJsonObject keys;
        keys["client_priv_key"] = lastConfig.value(config_key::client_priv_key);
        keys["client_pub_key"] = lastConfig.value(config_key::client_pub_key);
        keys["server_pub_key"] = lastConfig.value(config_key::server_pub_key);
        keys["psk_key"] = lastConfig.value(config_key::psk_key);
        result["keys"] = keys;

        QJsonObject awgParams;
        awgParams["Jc"] = lastConfig.value(config_key::junkPacketCount);
        awgParams["Jmin"] = lastConfig.value(config_key::junkPacketMinSize);
        awgParams["Jmax"] = lastConfig.value(config_key::junkPacketMaxSize);
        awgParams["S1"] = lastConfig.value(config_key::initPacketJunkSize);
        awgParams["S2"] = lastConfig.value(config_key::responsePacketJunkSize);
        awgParams["S3"] = lastConfig.value(config_key::cookieReplyPacketJunkSize);
        awgParams["S4"] = lastConfig.value(config_key::transportPacketJunkSize);
        awgParams["H1"] = lastConfig.value(config_key::initPacketMagicHeader);
        awgParams["H2"] = lastConfig.value(config_key::responsePacketMagicHeader);
        awgParams["H3"] = lastConfig.value(config_key::underloadPacketMagicHeader);
        awgParams["H4"] = lastConfig.value(config_key::transportPacketMagicHeader);
        result["awgParams"] = awgParams;

        if (containerType == DockerContainer::Awg || containerType == DockerContainer::Awg2) {
            QString port = protocolConfig.value(config_key::port).toString();
            if (port.isEmpty()) {
                port = lastConfig.value(config_key::port).toString();
            }
            if (port.isEmpty()) {
                // The raw wg-quick INI is the authoritative source for the endpoint/port.
                const QString iniConfig = protocolConfig.value(config_key::config).toString();
                const QRegularExpression endpointRe("Endpoint\\s*=\\s*([^\\s:]+):(\\d+)");
                const auto match = endpointRe.match(iniConfig);
                if (match.hasMatch()) {
                    port = match.captured(2);
                }
            }
            if (port.isEmpty()) {
                port = protocols::awg::defaultPort;
            }
            result["endpoint"] = QString("%1:%2").arg(serverConfig.value(config_key::hostName).toString(), port);
        }
    }

    return QString(QJsonDocument(result).toJson(QJsonDocument::Indented));
}

QString ApiConfigsController::getCurrentServerMtu()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto containers = serverConfig.value(config_key::containers).toArray();
    if (containers.isEmpty()) {
        return "";
    }

    auto containerObj = containers.at(0).toObject();
    QString containerName = ContainerProps::containerTypeToString(
            ContainerProps::containerFromString(containerObj.value(config_key::container).toString()));
    auto protocolConfig = containerObj.value(containerName).toObject();
    auto lastConfig = QJsonDocument::fromJson(protocolConfig.value(config_key::last_config).toString().toUtf8()).object();
    return lastConfig.value(config_key::mtu).toString();
}

QString ApiConfigsController::getCurrentServerDns()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    QString dns1 = serverConfig.value(config_key::dns1).toString();
    QString dns2 = serverConfig.value(config_key::dns2).toString();
    if (dns1.isEmpty() && dns2.isEmpty()) {
        return "";
    }
    if (dns2.isEmpty()) {
        return dns1;
    }
    if (dns1.isEmpty()) {
        return dns2;
    }
    return QString("%1, %2").arg(dns1, dns2);
}

QString ApiConfigsController::getCurrentServerClientIp()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    auto containers = serverConfig.value(config_key::containers).toArray();
    if (containers.isEmpty()) {
        return "";
    }

    auto containerObj = containers.at(0).toObject();
    QString containerName = ContainerProps::containerTypeToString(
            ContainerProps::containerFromString(containerObj.value(config_key::container).toString()));
    auto protocolConfig = containerObj.value(containerName).toObject();
    auto lastConfig = QJsonDocument::fromJson(protocolConfig.value(config_key::last_config).toString().toUtf8()).object();
    return lastConfig.value(config_key::client_ip).toString();
}

bool ApiConfigsController::fetchSubscriptionConfigs(const QString &subscriptionId)
{
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, subscriptionId]() { fetchSubscriptionConfigs(subscriptionId); }, Qt::QueuedConnection);
        return true;
    }

    qDebug() << "[SUBSCRIPTION] fetching configs for" << subscriptionId;
    m_subscriptionConfigs = QJsonArray();
    emit subscriptionConfigsChanged();

    QJsonObject authData;
    authData[apiDefs::key::apiKey] = subscriptionId;
    authData[apiDefs::key::id] = subscriptionId;

    QJsonObject servicesPayload;
    servicesPayload[configKey::osVersion] = QSysInfo::productType();
    servicesPayload[apiDefs::key::appLanguage] = m_settings->getAppLanguage().name().split("_").first();
    servicesPayload[configKey::authData] = authData;

    GatewayController gatewayController(m_settings->getGatewayEndpoint(), false, apiDefs::requestTimeoutMsecs, false);
    QByteArray servicesResponse;
    ErrorCode errorCode = gatewayController.post(QString("%1v1/services"), servicesPayload, servicesResponse);
    if (errorCode != ErrorCode::NoError) {
        qWarning() << "[SUBSCRIPTION] failed to fetch services:" << static_cast<int>(errorCode);
        emit errorOccurred(errorCode);
        return false;
    }

    QJsonObject servicesData = QJsonDocument::fromJson(servicesResponse).object();
    qDebug().noquote() << "[SUBSCRIPTION] /v1/services response:" << QJsonDocument(servicesData).toJson(QJsonDocument::Compact);

    QJsonArray services = servicesData.value(configKey::services).toArray();
    if (services.isEmpty()) {
        qWarning() << "[SUBSCRIPTION] no services found";
        emit errorOccurred(ErrorCode::ApiConfigEmptyError);
        return false;
    }

    bool anySuccess = false;
    QString userCountryCode = servicesData.value(configKey::userCountryCode).toString();

    for (const auto &service : services) {
        QJsonObject serviceObject = service.toObject();
        QString serviceType = serviceObject.value(configKey::serviceType).toString();
        QString serviceProtocol = serviceObject.value(configKey::serviceProtocol).toString();

        auto connections = serviceObject.value("connections").toArray();
        if (connections.isEmpty()) {
            auto availableCountries = serviceObject.value(configKey::availableCountries).toArray();
            for (const auto &country : availableCountries) {
                connections.append(country.toObject());
            }
        }
        if (connections.isEmpty()) {
            connections.append(QJsonObject {
                { configKey::countryCode, userCountryCode },
                { "connection_uuid", "" },
                { "connection_label", "" }
            });
        }

        for (const auto &connection : connections) {
            QJsonObject connectionObject = connection.toObject();
            QString serverCountryCode = connectionObject.value(configKey::countryCode).toString();
            QString connectionUuid = connectionObject.value("connection_uuid").toString();
            QString connectionLabel = connectionObject.value("connection_label").toString();
            // node_id pins the exact node (gateway v0.6.19+): connection_uuid alone is
            // shared by all nodes of one (env, protocol) group
            QString nodeId = connectionObject.value("node_id").toString();

            qDebug().noquote() << "[SUBSCRIPTION] connection:"
                               << "protocol=" << serviceProtocol
                               << "country=" << serverCountryCode
                               << "uuid=" << connectionUuid
                               << "node=" << nodeId
                               << "label=" << connectionLabel;

            ProtocolData protocolData = generateProtocolData(serviceProtocol);

            GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                                    QString(APP_VERSION),
                                                    m_settings->getAppLanguage().name().split("_").first(),
                                                    m_settings->getInstallationUuid(true),
                                                    userCountryCode,
                                                    serverCountryCode,
                                                    serviceType,
                                                    serviceProtocol,
                                                    authData };

            QJsonObject apiPayload = gatewayRequestData.toJsonObject();
            if (!connectionUuid.isEmpty()) {
                apiPayload["connection_id"] = connectionUuid;
            }
            if (!nodeId.isEmpty()) {
                apiPayload["node_id"] = nodeId;
            }
            appendProtocolDataToApiPayload(serviceProtocol, protocolData, apiPayload);

            QByteArray responseBody;
            errorCode = executeRequest(QString("%1v1/config"), apiPayload, responseBody);
            if (errorCode != ErrorCode::NoError) {
                qWarning() << "[SUBSCRIPTION] failed to fetch config for" << serviceProtocol << serverCountryCode << ":" << static_cast<int>(errorCode);
                continue;
            }

            QJsonObject serverConfig;
            errorCode = fillServerConfig(serviceProtocol, protocolData, responseBody, serverConfig);
            if (errorCode != ErrorCode::NoError) {
                qWarning() << "[SUBSCRIPTION] failed to fill config for" << serviceProtocol << serverCountryCode << ":" << static_cast<int>(errorCode);
                continue;
            }

            // fillServerConfig may have lost auth_data when the decrypted config didn't include it;
            // restore it from the request payload before saving.
            serverConfig.insert(configKey::authData, authData);

            QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
            apiConfig.insert(configKey::userCountryCode, serverCountryCode);
            apiConfig.insert(configKey::serviceType, serviceType);
            // Prefer the connection's own protocol from /v1/services (accurate since gateway v0.6.10),
            // then the gateway's api_config.service_protocol, then the service card protocol.
            const QString connectionProtocol = connectionObject.value("service_protocol").toString();
            if (!connectionProtocol.isEmpty()) {
                apiConfig.insert(configKey::serviceProtocol, connectionProtocol);
            } else if (apiConfig.value(configKey::serviceProtocol).toString().isEmpty()) {
                apiConfig.insert(configKey::serviceProtocol, serviceProtocol);
            }
            // Node environment from /v1/services (dev, wl, ru, experimental, production, custom<name>; empty for the placeholder)
            apiConfig.insert("env", connectionObject.value("env").toString());
            apiConfig.insert(configKey::authData, authData);
            apiConfig.insert("connection_uuid", connectionUuid);
            apiConfig.insert("node_id", nodeId);
            serverConfig.insert(configKey::apiConfig, apiConfig);
            serverConfig.insert(configKey::authData, authData);

            QString hostName = serverConfig.value(config_key::hostName).toString();
            QString protocolName = apiConfig.value(configKey::serviceProtocol).toString(serviceProtocol).toUpper();
            QString displayLabel = connectionLabel.remove(QChar(0x200D)).replace("<200d>", "");

            // connection_label comes as "<name> · <protocol details>" (e.g. "Suomi · AmneziaWG",
            // "Czech Republic Fast · VLESS TCP Reality"): keep only the name in the title and
            // move the protocol details to the small description line.
            QString name = displayLabel;
            QString protocolDetails;
            const int separatorPos = displayLabel.indexOf(QStringLiteral(" · "));
            if (separatorPos > 0) {
                name = displayLabel.left(separatorPos);
                protocolDetails = displayLabel.mid(separatorPos + 3);
            }
            if (name.isEmpty()) {
                name = QString("reelsoprovod %1 %2").arg(serverCountryCode.toUpper(), protocolName);
            }
            if (protocolDetails.isEmpty()) {
                protocolDetails = protocolName;
            }
            QString description = QString("%1 · %2").arg(protocolDetails, hostName);

            serverConfig[config_key::name] = name;
            serverConfig[config_key::description] = description;

            QJsonObject displayInfo;
            displayInfo["countryCode"] = serverCountryCode.toUpper();
            displayInfo["countryName"] = connectionObject.value(configKey::countryCode).toString().toUpper();
            displayInfo["protocol"] = protocolName;
            displayInfo["hostName"] = hostName;
            displayInfo["serviceName"] = serviceObject.value("service_info").toObject().value("name").toString();
            displayInfo["connectionUuid"] = connectionUuid;
            displayInfo["connectionLabel"] = displayLabel;
            serverConfig["displayInfo"] = displayInfo;

            m_subscriptionConfigs.append(serverConfig);
            anySuccess = true;
        }
    }

    // Add display index for duplicate labels within same country
    QMap<QString, int> labelCounts;
    for (const auto &config : m_subscriptionConfigs) {
        QJsonObject displayInfo = config.toObject().value("displayInfo").toObject();
        QString key = displayInfo.value("connectionLabel").toString() + "|" + displayInfo.value("countryCode").toString();
        labelCounts[key]++;
    }

    QJsonArray indexedConfigs;
    QMap<QString, int> labelCurrent;
    for (const auto &config : m_subscriptionConfigs) {
        QJsonObject configObject = config.toObject();
        QJsonObject displayInfo = configObject.value("displayInfo").toObject();
        QString key = displayInfo.value("connectionLabel").toString() + "|" + displayInfo.value("countryCode").toString();
        int displayIndex = 0;
        if (labelCounts[key] > 1) {
            labelCurrent[key]++;
            displayIndex = labelCurrent[key];
        }
        displayInfo["displayIndex"] = displayIndex;

        QString baseName = configObject.value(config_key::name).toString();
        if (displayIndex > 0) {
            baseName += QString(" #%1").arg(displayIndex);
        }
        configObject[config_key::name] = baseName;

        configObject.insert("displayInfo", displayInfo);
        indexedConfigs.append(configObject);
    }
    m_subscriptionConfigs = indexedConfigs;

    emit subscriptionConfigsChanged();
    qDebug() << "[SUBSCRIPTION] fetch done, success:" << anySuccess << "configs count:" << m_subscriptionConfigs.size();
    return anySuccess;
}

QVariantList ApiConfigsController::getSubscriptionConfigs() const
{
    QVariantList list;
    for (const auto &config : m_subscriptionConfigs) {
        list.append(config.toObject().toVariantMap());
    }
    return list;
}

bool ApiConfigsController::reloadSubscriptionConfigs()
{
    const QString subscriptionId = resolveSubscriptionId();

    if (subscriptionId.isEmpty()) {
        qWarning() << "[SUBSCRIPTION] reload: no subscription id found";
        return false;
    }

    if (!fetchSubscriptionConfigs(subscriptionId)) {
        return false;
    }

    // remove previously imported subscription servers (they carry connection_uuid)
    for (int i = m_serversModel->getServersCount() - 1; i >= 0; --i) {
        const QJsonObject serverConfig = m_serversModel->getServerConfig(i);
        const QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
        if (!apiConfig.value("connection_uuid").toString().isEmpty()) {
            m_serversModel->removeServer(i);
        }
    }

    bool anyInstalled = false;
    for (int i = 0; i < m_subscriptionConfigs.size(); ++i) {
        if (installSubscriptionConfig(i)) {
            anyInstalled = true;
        }
    }
    return anyInstalled;
}

bool ApiConfigsController::installSubscriptionConfig(int index)
{
    qDebug() << "[SUBSCRIPTION] install config index:" << index;
    if (index < 0 || index >= m_subscriptionConfigs.size()) {
        qDebug() << "[SUBSCRIPTION] invalid index";
        return false;
    }

    QJsonObject serverConfig = m_subscriptionConfigs.at(index).toObject();
    QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
    QString serviceProtocol = apiConfig.value(configKey::serviceProtocol).toString();
    QString serverCountryCode = apiConfig.value(configKey::userCountryCode).toString();
    QString connectionUuid = apiConfig.value("connection_uuid").toString();

    qDebug() << "[SUBSCRIPTION] protocol:" << serviceProtocol << "country:" << serverCountryCode << "connection:" << connectionUuid;

    QJsonObject authData = apiConfig.value(configKey::authData).toObject();
    if (authData.isEmpty()) {
        authData = serverConfig.value(configKey::authData).toObject();
    }
    if (!authData.contains(apiDefs::key::apiKey) && authData.contains(apiDefs::key::id)) {
        authData[apiDefs::key::apiKey] = authData.value(apiDefs::key::id);
    }
    serverConfig.insert(configKey::authData, authData);
    apiConfig.insert(configKey::authData, authData);
    serverConfig.insert(configKey::apiConfig, apiConfig);

    int serversBefore = m_serversModel->getServersCount();
    QString serverName = serverConfig.value(config_key::name).toString();
    QString serverDescription = serverConfig.value(config_key::description).toString();

    if (m_serversModel->isServerFromApiAlreadyExists(serverName, serverDescription)) {
        qDebug() << "[SUBSCRIPTION] duplicate name/description:" << serverName << serverDescription;
    } else {
        qDebug() << "[SUBSCRIPTION] adding server" << serverName;
        m_serversModel->addServer(serverConfig);
    }

    int serversAfter = m_serversModel->getServersCount();
    qDebug() << "[SUBSCRIPTION] count before:" << serversBefore << "after:" << serversAfter;
    return serversAfter > serversBefore;
}

QList<QString> ApiConfigsController::getQrCodes()
{
    return m_qrCodes;
}

int ApiConfigsController::getQrCodesCount()
{
    return static_cast<int>(m_qrCodes.size());
}

QString ApiConfigsController::getVpnKey()
{
    return m_vpnKey;
}

ErrorCode ApiConfigsController::importServiceFromBilling(const QByteArray &responseBody, const bool isTestPurchase)
{
#ifdef Q_OS_IOS
    QJsonObject responseObject = QJsonDocument::fromJson(responseBody).object();
    QString key = responseObject.value(QStringLiteral("key")).toString();
    if (key.isEmpty()) {
        qWarning().noquote() << "[IAP] Subscription response does not contain a key field";
        return ErrorCode::ApiPurchaseError;
    }

    if (m_serversModel->hasServerWithVpnKey(key)) {
        qInfo().noquote() << "[IAP] Subscription config with the same vpn_key already exists";
        return ErrorCode::ApiConfigAlreadyAdded;
    }

    QString normalizedKey = key;
    normalizedKey.replace(QStringLiteral("vpn://"), QString());

    QByteArray configString = QByteArray::fromBase64(normalizedKey.toUtf8(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QByteArray configUncompressed = qUncompress(configString);
    if (!configUncompressed.isEmpty()) {
        configString = configUncompressed;
    }

    if (configString.isEmpty()) {
        qWarning().noquote() << "[IAP] Subscription response config payload is empty";
        return ErrorCode::ApiPurchaseError;
    }

    QJsonObject configObject = QJsonDocument::fromJson(configString).object();

    quint16 crc = qChecksum(QJsonDocument(configObject).toJson());
    auto apiConfig = configObject.value(apiDefs::key::apiConfig).toObject();
    apiConfig[apiDefs::key::vpnKey] = normalizedKey;
    apiConfig[apiDefs::key::isTestPurchase] = isTestPurchase;

    configObject.insert(apiDefs::key::apiConfig, apiConfig);
    configObject.insert(config_key::crc, crc);
    m_serversModel->addServer(configObject);

    return ErrorCode::NoError;
#else
    Q_UNUSED(responseBody)
    Q_UNUSED(isTestPurchase)
    return ErrorCode::NoError;
#endif
}

ErrorCode ApiConfigsController::executeRequest(const QString &endpoint, const QJsonObject &apiPayload, QByteArray &responseBody,
                                               bool isTestPurchase)
{
    qDebug().noquote() << "[AGW EXECUTE] endpoint:" << endpoint.arg(m_settings->getGatewayEndpoint(isTestPurchase))
                       << "payload keys:" << apiPayload.keys();
    GatewayController gatewayController(m_settings->getGatewayEndpoint(isTestPurchase), m_settings->isDevGatewayEnv(isTestPurchase),
                                        apiDefs::requestTimeoutMsecs, m_settings->isStrictKillSwitchEnabled());
    return gatewayController.post(endpoint, apiPayload, responseBody);
}
