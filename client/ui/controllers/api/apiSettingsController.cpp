#include "apiSettingsController.h"

#include "core/api/apiUtils.h"
#include "core/controllers/gatewayController.h"
#include "core/api/apiDefs.h"
#include "platforms/ios/ios_controller.h"
#include "version.h"

namespace
{
    namespace configKey
    {
        constexpr char userCountryCode[] = "user_country_code";
        constexpr char serverCountryCode[] = "server_country_code";
        constexpr char serviceType[] = "service_type";
        constexpr char serviceInfo[] = "service_info";

        constexpr char apiConfig[] = "api_config";
        constexpr char authData[] = "auth_data";
    }

    const int requestTimeoutMsecs = 12 * 1000; // 12 secs

    // Stable identity of a server row: subscription + exact node. A row index is not
    // stable, rows are renumbered whenever the server list changes.
    QString accountInfoCacheKey(const QJsonObject &serverConfig)
    {
        const QJsonObject apiConfig = serverConfig.value(configKey::apiConfig).toObject();
        QJsonObject authData = apiConfig.value(configKey::authData).toObject();
        if (authData.isEmpty()) {
            authData = serverConfig.value(configKey::authData).toObject();
        }
        QString subscription = authData.value(apiDefs::key::apiKey).toString();
        if (subscription.isEmpty()) {
            subscription = authData.value(apiDefs::key::id).toString();
        }
        return subscription + QLatin1Char('|') + apiConfig.value(QStringLiteral("connection_uuid")).toString() + QLatin1Char('|')
                + apiConfig.value(QStringLiteral("node_id")).toString();
    }
}

ApiSettingsController::ApiSettingsController(const QSharedPointer<ServersModel> &serversModel,
                                             const QSharedPointer<ApiAccountInfoModel> &apiAccountInfoModel,
                                             const QSharedPointer<ApiCountryModel> &apiCountryModel,
                                             const QSharedPointer<ApiDevicesModel> &apiDevicesModel,
                                             const std::shared_ptr<Settings> &settings, QObject *parent)
    : QObject(parent),
      m_serversModel(serversModel),
      m_apiAccountInfoModel(apiAccountInfoModel),
      m_apiCountryModel(apiCountryModel),
      m_apiDevicesModel(apiDevicesModel),
      m_settings(settings)
{
}

ApiSettingsController::~ApiSettingsController()
{
}

bool ApiSettingsController::getAccountInfo(bool reload, bool forceRefresh)
{
    auto processedIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfig = m_serversModel->getServerConfig(processedIndex);
    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();

    // Shared connections authenticate by share_token, which the backend accepts
    // ONLY on /v1/config — any other endpoint (incl. account_info) returns 403.
    // Serve them locally regardless of reload/cache state.
    const auto apiAuthData = apiConfig.value(configKey::authData).toObject();
    const auto rootAuthDataShared = serverConfig.value(configKey::authData).toObject();
    const bool isShared = apiAuthData.contains(QStringLiteral("share_token"))
                          || rootAuthDataShared.contains(QStringLiteral("share_token"));

    // When just opening the settings screen we can show whatever we have stored locally
    // without waiting for an API round-trip. Refresh/reload still hits the server.
    if (!reload || isShared) {
        QJsonObject localAccountInfo;
        localAccountInfo[apiDefs::key::availableCountries] = apiConfig.value(apiDefs::key::availableCountries).toArray();
        localAccountInfo[apiDefs::key::supportedProtocols] = apiConfig.value(apiDefs::key::supportedProtocols).toArray();

        QString serviceType = apiConfig.value(configKey::serviceType).toString();
        if (serviceType == "amnezia-free") {
            localAccountInfo[apiDefs::key::subscriptionDescription] = "FRKN Free subscription";
        } else if (serviceType == "amnezia-premium") {
            localAccountInfo[apiDefs::key::subscriptionDescription] = "FRKN Premium subscription";
        } else if (serviceType == "external-premium") {
            localAccountInfo[apiDefs::key::subscriptionDescription] = "External Premium subscription";
        } else {
            localAccountInfo[apiDefs::key::subscriptionDescription] = serviceType;
        }

        m_apiAccountInfoModel->updateModel(localAccountInfo, serverConfig);
        updateApiCountryModel();
        return true;
    }

    // serve a fresh-enough cached response instead of an API round-trip
    const QString cacheKey = accountInfoCacheKey(serverConfig);
    if (!forceRefresh) {
        auto infoIt = m_accountInfoCache.constFind(cacheKey);
        auto timeIt = m_accountInfoCacheTime.constFind(cacheKey);
        if (infoIt != m_accountInfoCache.constEnd() && timeIt != m_accountInfoCacheTime.constEnd()
            && timeIt->secsTo(QDateTime::currentDateTime()) < kAccountInfoCacheTtlSecs) {
            m_apiAccountInfoModel->updateModel(infoIt.value(), serverConfig);
            updateApiCountryModel();
            updateApiDevicesModel();
            return true;
        }
        // several warm-up triggers fire together at startup — fetch once
        if (m_accountInfoInFlight.contains(cacheKey)) {
            return true;
        }
    }
    m_accountInfoInFlight.insert(cacheKey);

    auto rootAuthData = serverConfig.value(configKey::authData).toObject();

    qDebug().noquote() << "[ACCOUNT INFO] serverIndex:" << processedIndex
                       << "configVersion:" << serverConfig.value("config_version").toInt()
                       << "apiConfig keys:" << apiConfig.keys()
                       << "rootAuthData keys:" << rootAuthData.keys()
                       << "rootAuthData:" << QJsonDocument(rootAuthData).toJson(QJsonDocument::Compact);

    QJsonObject authData = apiConfig.value(configKey::authData).toObject();
    if (authData.isEmpty()) {
        authData = rootAuthData;
    }
    if (!authData.contains(apiDefs::key::apiKey) && authData.contains(apiDefs::key::id)) {
        authData[apiDefs::key::apiKey] = authData.value(apiDefs::key::id);
    }

    qDebug().noquote() << "[ACCOUNT INFO] authData after fix:" << QJsonDocument(authData).toJson(QJsonDocument::Compact);

    // Nothing to authenticate with (e.g. the start-up warm-up fired before a
    // default server was selected, processedIndex == -1): skip quietly instead
    // of sending empty auth_data and getting a 400 "Missing subscription id".
    if (!authData.contains(apiDefs::key::apiKey) && !authData.contains(QStringLiteral("share_token"))) {
        qWarning().noquote() << "[ACCOUNT INFO] no auth data (serverIndex:" << processedIndex << ") — skipping request";
        m_accountInfoInFlight.remove(cacheKey);
        return false;
    }

    bool isTestPurchase = apiConfig.value(apiDefs::key::isTestPurchase).toBool(false);
    auto gatewayController = QSharedPointer<GatewayController>::create(
            m_settings->getGatewayEndpoint(isTestPurchase), m_settings->isDevGatewayEnv(isTestPurchase), requestTimeoutMsecs,
            m_settings->isStrictKillSwitchEnabled(), nullptr, m_settings->getGatewayEndpointFallback(isTestPurchase));

    QJsonObject apiPayload;
    apiPayload[configKey::userCountryCode] = apiConfig.value(configKey::userCountryCode).toString();
    apiPayload[configKey::serviceType] = apiConfig.value(configKey::serviceType).toString();
    apiPayload[configKey::authData] = authData;
    apiPayload[apiDefs::key::cliVersion] = QString(APP_VERSION);
    apiPayload[apiDefs::key::appLanguage] = m_settings->getAppLanguage().name().split("_").first();

    // async: the UI thread must not block on the gateway round-trip; the result
    // is applied from the continuation (cache/inFlight bookkeeping lives there)
    auto future = gatewayController->postAsync(QString("%1v1/account_info"), apiPayload);
    future.then(this, [this, gatewayController, processedIndex, cacheKey, serverConfig, reload, forceRefresh](QPair<ErrorCode, QByteArray> result) {
        const auto [errorCode, responseBody] = result;
        m_accountInfoInFlight.remove(cacheKey);
        if (errorCode != ErrorCode::NoError) {
            if (forceRefresh) {
                emit errorOccurred(errorCode);
            } else {
                qWarning() << "[ACCOUNT INFO] background fetch failed:" << static_cast<int>(errorCode);
            }
            return;
        }
        // the user may have switched to another server while the request was in
        // flight — applying the stale response would poison the wrong server card
        const int currentIndex = m_serversModel->getProcessedServerIndex();
        if (currentIndex != processedIndex || accountInfoCacheKey(m_serversModel->getServerConfig(currentIndex)) != cacheKey) {
            qDebug() << "[ACCOUNT INFO] dropping stale response for serverIndex:" << processedIndex;
            return;
        }

        QJsonObject accountInfo = QJsonDocument::fromJson(responseBody).object();
        m_accountInfoCache[cacheKey] = accountInfo;
        m_accountInfoCacheTime[cacheKey] = QDateTime::currentDateTime();
        m_apiAccountInfoModel->updateModel(accountInfo, serverConfig);

        if (reload) {
            updateApiCountryModel();
            updateApiDevicesModel();
        }
    });

    return true;
}

void ApiSettingsController::updateApiCountryModel()
{
    m_apiCountryModel->updateModel(m_apiAccountInfoModel->getAvailableCountries(), "");
    m_apiCountryModel->updateIssuedConfigsInfo(m_apiAccountInfoModel->getIssuedConfigsInfo());
}

void ApiSettingsController::updateApiDevicesModel()
{
    m_apiDevicesModel->updateModel(m_apiAccountInfoModel->getIssuedConfigsInfo());
}
