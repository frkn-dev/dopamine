#include "apiConfigsController.h"

#include "amnezia_application.h"
#include "configurators/wireguard_configurator.h"
#include "core/api/apiDefs.h"
#include "core/api/apiUtils.h"
#include "core/controllers/gatewayController.h"
#include "core/qrCodeUtils.h"
#include "ui/controllers/systemController.h"
#include "version.h"
#include <QClipboard>
#include <QDebug>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

#include "platforms/ios/ios_controller.h"

namespace
{
    namespace configKey
    {
        constexpr char cloak[] = "cloak";
        constexpr char awg[] = "awg";
        constexpr char vless[] = "vless";

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
        } else if (protocol == configKey::awg) {
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
        } else if (protocol == configKey::awg) {
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
        QByteArray ba = QByteArray::fromBase64(data.toUtf8(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

        if (ba.isEmpty()) {
            qDebug() << "empty vpn key";
            return ErrorCode::ApiConfigEmptyError;
        }

        QByteArray ba_uncompressed = qUncompress(ba);
        if (!ba_uncompressed.isEmpty()) {
            ba = ba_uncompressed;
        }

        QString configStr = ba;
        if (protocol == configKey::cloak) {
            configStr.replace("<key>", "<key>\n");
            configStr.replace("$OPENVPN_PRIV_KEY", apiPayloadData.certRequest.privKey);
        } else if (protocol == configKey::awg) {
            configStr.replace("$WIREGUARD_CLIENT_PRIVATE_KEY", apiPayloadData.wireGuardClientPrivKey);
            auto newServerConfig = QJsonDocument::fromJson(configStr.toUtf8()).object();
            auto containers = newServerConfig.value(config_key::containers).toArray();
            if (containers.isEmpty()) {
                qDebug() << "missing containers field";
                return ErrorCode::ApiConfigEmptyError;
            }
            auto containerObject = containers.at(0).toObject();
            auto containerType = ContainerProps::containerFromString(containerObject.value(config_key::container).toString());
            QString containerName = ContainerProps::containerTypeToString(containerType);
            auto serverProtocolConfig = containerObject.value(containerName).toObject();
            auto clientProtocolConfig =
                    QJsonDocument::fromJson(serverProtocolConfig.value(config_key::last_config).toString().toUtf8()).object();

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

            //

            containerObject[containerName] = serverProtocolConfig;
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
            serverConfig[config_key::description] = newServerConfig.value(config_key::description);
            serverConfig[config_key::name] = newServerConfig.value(config_key::name);
        }

        auto defaultContainer = newServerConfig.value(config_key::defaultContainer).toString();
        serverConfig[config_key::defaultContainer] = defaultContainer;

        qDebug().noquote() << "[API CONFIG] defaultContainer:" << defaultContainer;
        qDebug().noquote() << "[API CONFIG] containers:" << QJsonDocument(newServerConfig.value(config_key::containers).toArray()).toJson(QJsonDocument::Compact);

        QVariantMap map = serverConfig.value(configKey::apiConfig).toObject().toVariantMap();
        map.insert(newServerConfig.value(configKey::apiConfig).toObject().toVariantMap());
        auto apiConfig = QJsonObject::fromVariantMap(map);

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
}

QString ApiConfigsController::getSubscriptionId() const
{
    return m_subscriptionId;
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
    if (!email.isEmpty()) {
        body["email"] = email;
    } else {
        body["user"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    body["referred_by"] = referralCode.isEmpty() ? QString("WEB") : referralCode;
    body["language"] = m_settings->getAppLanguage().name().split("_").first();

    QNetworkRequest request;
    request.setUrl(QUrl("https://api.frkn.org/trial"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);

    QNetworkReply *reply = amnApp->networkManager()->post(request, QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        QByteArray responseData = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[TRIAL] request failed:" << reply->errorString() << "response:" << responseData;
            emit errorOccurred(ErrorCode::ApiConfigDecryptionError);
            return;
        }

        QJsonObject response = QJsonDocument::fromJson(responseData).object();
        QString id = response.value("response").toObject().value("id").toString();
        if (id.isEmpty()) {
            qWarning() << "[TRIAL] no subscription id in response:" << responseData;
            emit errorOccurred(ErrorCode::ApiConfigDecryptionError);
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
    QEventLoop waitProducts;
    bool productsFetched = false;
    QString productPrice;
    QString productCurrency;
    
    IosController::Instance()->fetchProducts(QStringList() << QStringLiteral("frkn_premium_6_month"),
                                             [&](const QList<QVariantMap> &products,
                                                 const QStringList &invalidIds,
                                                 const QString &errorString) {
                                                 if (!errorString.isEmpty() || products.isEmpty()) {
                                                     qWarning().noquote() << "[IAP] Failed to fetch product price:" << errorString;
                                                 } else {
                                                     const auto &product = products.first();
                                                     productPrice = product.value("price").toString();
                                                     productCurrency = product.value("currencyCode").toString();
                                                     productsFetched = true;
                                                     qInfo().noquote() << "[IAP] Fetched product price:" << productPrice << productCurrency;
                                                 }
                                                 waitProducts.quit();
                                             });
    waitProducts.exec();
    
    if (productsFetched && !productPrice.isEmpty()) {
        QJsonArray services = data.value("services").toArray();
        for (int i = 0; i < services.size(); ++i) {
            QJsonObject service = services[i].toObject();
            if (service.value(configKey::serviceType).toString() == serviceType::amneziaPremium) {
                QJsonObject serviceInfo = service.value(configKey::serviceInfo).toObject();
                QString formattedPrice = productPrice;
                if (!productCurrency.isEmpty()) {
                    formattedPrice += " " + productCurrency;
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
    bool purchaseOk = false;
    QString originalTransactionId;
    QString storeTransactionId;
    QString storeProductId;
    QString purchaseError;
    QEventLoop waitPurchase;
    IosController::Instance()->purchaseProduct(QStringLiteral("frkn_premium_6_month"),
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
    authData["id"] = m_subscriptionId.isEmpty() ? m_settings->getInstallationUuid(true) : m_subscriptionId;

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

        QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
        apiConfig.insert(configKey::userCountryCode, serverCountryCode);
        apiConfig.insert(configKey::serviceType, m_apiServicesModel->getSelectedServiceType());
        apiConfig.insert(configKey::serviceProtocol, m_apiServicesModel->getSelectedServiceProtocol());

        serverConfig.insert(configKey::apiConfig, apiConfig);

        QString hostName = serverConfig.value(config_key::hostName).toString();
        QString protocolName = gatewayRequestData.serviceProtocol.toUpper();
        QString name = QString("FRKN %1").arg(serverCountryCode.toUpper());
        QString description = QString("%1 %2").arg(protocolName, hostName);

        serverConfig[config_key::name] = name;
        serverConfig[config_key::description] = description;

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
                                                    bool reloadServiceConfig)
{
    auto serverConfig = m_serversModel->getServerConfig(serverIndex);
    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();

    GatewayRequestData gatewayRequestData { QSysInfo::productType(),
                                            QString(APP_VERSION),
                                            m_settings->getAppLanguage().name().split("_").first(),
                                            m_settings->getInstallationUuid(true),
                                            apiConfig.value(configKey::userCountryCode).toString(),
                                            newCountryCode,
                                            apiConfig.value(configKey::serviceType).toString(),
                                            apiConfig.value(configKey::serviceProtocol).toString(),
                                            serverConfig.value(configKey::authData).toObject() };

    ProtocolData protocolData = generateProtocolData(gatewayRequestData.serviceProtocol);

    QJsonObject apiPayload = gatewayRequestData.toJsonObject();
    appendProtocolDataToApiPayload(gatewayRequestData.serviceProtocol, protocolData, apiPayload);

    if (newCountryCode.isEmpty() && newCountryName.isEmpty() && !reloadServiceConfig) {
        apiPayload.insert(configKey::isConnectEvent, true);
    }

    bool isTestPurchase = apiConfig.value(apiDefs::key::isTestPurchase).toBool(false);
    QByteArray responseBody;
    ErrorCode errorCode = executeRequest(QString("%1v1/config"), apiPayload, responseBody, isTestPurchase);

    QJsonObject newServerConfig;
    if (errorCode == ErrorCode::NoError) {
        errorCode = fillServerConfig(gatewayRequestData.serviceProtocol, protocolData, responseBody, newServerConfig);
        if (errorCode != ErrorCode::NoError) {
            emit errorOccurred(errorCode);
            return false;
        }

        QJsonObject newApiConfig = newServerConfig.value(configKey::apiConfig).toObject();
        newApiConfig.insert(configKey::userCountryCode, apiConfig.value(configKey::userCountryCode));
        newApiConfig.insert(configKey::serviceType, apiConfig.value(configKey::serviceType));
        newApiConfig.insert(configKey::serviceProtocol, apiConfig.value(configKey::serviceProtocol));
        newApiConfig.insert(apiDefs::key::vpnKey, apiConfig.value(apiDefs::key::vpnKey));

        newServerConfig.insert(configKey::apiConfig, newApiConfig);
        newServerConfig.insert(configKey::authData, gatewayRequestData.authData);
        newServerConfig.insert(config_key::crc, serverConfig.value(config_key::crc));

        if (serverConfig.value(config_key::nameOverriddenByUser).toBool()) {
            newServerConfig.insert(config_key::name, serverConfig.value(config_key::name));
            newServerConfig.insert(config_key::nameOverriddenByUser, true);
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
        emit errorOccurred(errorCode);
        return false;
    }
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

    if (configSource == apiDefs::ConfigSource::Telegram
        && !m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()) {
        m_serversModel->removeApiConfig(serverIndex);
        return updateServiceFromTelegram(serverIndex);
    } else if (configSource == apiDefs::ConfigSource::AmneziaGateway
               && !m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()) {
        return updateServiceFromGateway(serverIndex, "", "");
    } else if (configSource && m_serversModel->isApiKeyExpired(serverIndex)) {
        qDebug() << "attempt to update api config by expires_at event";
        if (configSource == apiDefs::ConfigSource::AmneziaGateway) {
            return updateServiceFromGateway(serverIndex, "", "");
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

QString ApiConfigsController::getCurrentServerConfigJson()
{
    auto serverConfig = m_serversModel->getServerConfig(m_serversModel->getProcessedServerIndex());
    return QString(QJsonDocument(serverConfig).toJson(QJsonDocument::Indented));
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
    authData["id"] = subscriptionId;

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

            qDebug().noquote() << "[SUBSCRIPTION] connection:"
                               << "protocol=" << serviceProtocol
                               << "country=" << serverCountryCode
                               << "uuid=" << connectionUuid
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

            QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
            apiConfig.insert(configKey::userCountryCode, serverCountryCode);
            apiConfig.insert(configKey::serviceType, serviceType);
            apiConfig.insert(configKey::serviceProtocol, serviceProtocol);
            apiConfig.insert(configKey::authData, authData);
            apiConfig.insert("connection_uuid", connectionUuid);
            serverConfig.insert(configKey::apiConfig, apiConfig);

            QString hostName = serverConfig.value(config_key::hostName).toString();
            QString protocolName = serviceProtocol.toUpper();
            QString displayLabel = connectionLabel.remove(QChar(0x200D)).replace("<200d>", "");
            QString name = displayLabel.isEmpty() ? QString("FRKN %1").arg(serverCountryCode.toUpper()) : displayLabel;
            QString description = QString("%1 %2").arg(protocolName, hostName);

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
    GatewayController gatewayController(m_settings->getGatewayEndpoint(isTestPurchase), m_settings->isDevGatewayEnv(isTestPurchase),
                                        apiDefs::requestTimeoutMsecs, m_settings->isStrictKillSwitchEnabled());
    return gatewayController.post(endpoint, apiPayload, responseBody);
}
