#include "apiSettingsController.h"

#include <QEventLoop>
#include <QTimer>

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

bool ApiSettingsController::getAccountInfo(bool reload)
{
    auto processedIndex = m_serversModel->getProcessedServerIndex();
    auto serverConfig = m_serversModel->getServerConfig(processedIndex);
    auto apiConfig = serverConfig.value(configKey::apiConfig).toObject();

    // When just opening the settings screen we can show whatever we have stored locally
    // without waiting for an API round-trip. Refresh/reload still hits the server.
    if (!reload) {
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

    if (reload) {
        QEventLoop wait;
        QTimer::singleShot(1000, &wait, &QEventLoop::quit);
        wait.exec(QEventLoop::ExcludeUserInputEvents);
    }

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

    bool isTestPurchase = apiConfig.value(apiDefs::key::isTestPurchase).toBool(false);
    GatewayController gatewayController(m_settings->getGatewayEndpoint(isTestPurchase), m_settings->isDevGatewayEnv(isTestPurchase),
                                        requestTimeoutMsecs, m_settings->isStrictKillSwitchEnabled());

    QJsonObject apiPayload;
    apiPayload[configKey::userCountryCode] = apiConfig.value(configKey::userCountryCode).toString();
    apiPayload[configKey::serviceType] = apiConfig.value(configKey::serviceType).toString();
    apiPayload[configKey::authData] = authData;
    apiPayload[apiDefs::key::cliVersion] = QString(APP_VERSION);
    apiPayload[apiDefs::key::appLanguage] = m_settings->getAppLanguage().name().split("_").first();

    QByteArray responseBody;

    ErrorCode errorCode = gatewayController.post(QString("%1v1/account_info"), apiPayload, responseBody);
    if (errorCode != ErrorCode::NoError) {
        emit errorOccurred(errorCode);
        return false;
    }

    QJsonObject accountInfo = QJsonDocument::fromJson(responseBody).object();
    m_apiAccountInfoModel->updateModel(accountInfo, serverConfig);

    if (reload) {
        updateApiCountryModel();
        updateApiDevicesModel();
    }

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
