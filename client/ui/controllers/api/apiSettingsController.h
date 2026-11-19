#ifndef APISETTINGSCONTROLLER_H
#define APISETTINGSCONTROLLER_H

#include <QObject>

#include "ui/models/api/apiAccountInfoModel.h"
#include "ui/models/api/apiCountryModel.h"
#include "ui/models/api/apiDevicesModel.h"
#include "ui/models/servers_model.h"

class ApiSettingsController : public QObject
{
    Q_OBJECT
public:
    ApiSettingsController(const QSharedPointer<ServersModel> &serversModel, const QSharedPointer<ApiAccountInfoModel> &apiAccountInfoModel,
                          const QSharedPointer<ApiCountryModel> &apiCountryModel, const QSharedPointer<ApiDevicesModel> &apiDevicesModel,
                          const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);
    ~ApiSettingsController();

public slots:
    // false only when the request cannot even be started (no auth data); true means
    // local/cached data was applied or the async fetch was launched — the fresh
    // account info arrives via the account info model updates
    bool getAccountInfo(bool reload, bool forceRefresh = false);
    void updateApiCountryModel();
    void updateApiDevicesModel();

signals:
    void errorOccurred(ErrorCode errorCode);

private:
    QSharedPointer<ServersModel> m_serversModel;
    QSharedPointer<ApiAccountInfoModel> m_apiAccountInfoModel;
    QSharedPointer<ApiCountryModel> m_apiCountryModel;
    QSharedPointer<ApiDevicesModel> m_apiDevicesModel;

    std::shared_ptr<Settings> m_settings;

    // account_info cache per server index — opening the server card must not
    // hit the gateway every time; refreshed on app start and when older than TTL
    QHash<QString, QJsonObject> m_accountInfoCache;
    QHash<QString, QDateTime> m_accountInfoCacheTime;
    QSet<QString> m_accountInfoInFlight;
    static constexpr qint64 kAccountInfoCacheTtlSecs = 3600;
};

#endif // APISETTINGSCONTROLLER_H
