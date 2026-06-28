#ifndef APICONFIGSCONTROLLER_H
#define APICONFIGSCONTROLLER_H

#include <QObject>

#include "configurators/openvpn_configurator.h"
#include "ui/models/api/apiServicesModel.h"
#include "ui/models/servers_model.h"

struct ProtocolData
{
    OpenVpnConfigurator::ConnectionData certRequest;

    QString wireGuardClientPrivKey;
    QString wireGuardClientPubKey;

    QString xrayUuid;
};

class ApiConfigsController : public QObject
{
    Q_OBJECT
public:
    ApiConfigsController(const QSharedPointer<ServersModel> &serversModel, const QSharedPointer<ApiServicesModel> &apiServicesModel,
                         const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);

    Q_PROPERTY(QList<QString> qrCodes READ getQrCodes NOTIFY vpnKeyExportReady)
    Q_PROPERTY(int qrCodesCount READ getQrCodesCount NOTIFY vpnKeyExportReady)
    Q_PROPERTY(QString vpnKey READ getVpnKey NOTIFY vpnKeyExportReady)
    Q_PROPERTY(QString subscriptionId READ getSubscriptionId WRITE setSubscriptionId NOTIFY subscriptionIdChanged)
    Q_PROPERTY(QString selectedServerCountryCode READ getSelectedServerCountryCode WRITE setSelectedServerCountryCode NOTIFY selectedServerCountryCodeChanged)
    Q_PROPERTY(bool importAllCountries READ getImportAllCountries WRITE setImportAllCountries NOTIFY importAllCountriesChanged)
    Q_PROPERTY(QVariantList subscriptionConfigs READ getSubscriptionConfigs NOTIFY subscriptionConfigsChanged)

public slots:
    bool exportNativeConfig(const QString &serverCountryCode, const QString &fileName);
    bool revokeNativeConfig(const QString &serverCountryCode);
    bool exportVpnKey(const QString &fileName);
    void prepareVpnKeyExport();
    void copyVpnKeyToClipboard();

    bool fillAvailableServices();
    bool importService();
    bool importSerivceFromAppStore();
    bool restoreSerivceFromAppStore();
    bool importServiceFromGateway();

    QString getSubscriptionId() const;
    void setSubscriptionId(const QString &subscriptionId);
    Q_INVOKABLE bool createTrial(const QString &email, const QString &referralCode = "WEB");

    QString getSelectedServerCountryCode() const;
    void setSelectedServerCountryCode(const QString &countryCode);

    bool getImportAllCountries() const;
    void setImportAllCountries(bool importAll);

    bool updateServiceFromGateway(const int serverIndex, const QString &newCountryCode, const QString &newCountryName,
                                  bool reloadServiceConfig = false);
    bool updateServiceFromTelegram(const int serverIndex);
    bool deactivateDevice(const bool isRemoveEvent);
    bool deactivateExternalDevice(const QString &uuid, const QString &serverCountryCode);

    bool isConfigValid();

    void setCurrentProtocol(const QString &protocolName);
    bool isVlessProtocol();

    Q_INVOKABLE QString getCurrentServerConfigJson();

    Q_INVOKABLE bool fetchSubscriptionConfigs(const QString &subscriptionId);
    Q_INVOKABLE bool installSubscriptionConfig(int index);

    QVariantList getSubscriptionConfigs() const;

signals:
    void errorOccurred(ErrorCode errorCode);
    void subscriptionIdChanged();
    void selectedServerCountryCodeChanged();
    void importAllCountriesChanged();
    void subscriptionConfigsChanged();

    void installServerFromApiFinished(const QString &message);
    void changeApiCountryFinished(const QString &message);
    void reloadServerFromApiFinished(const QString &message);
    void updateServerFromApiFinished();

    void vpnKeyExportReady();

private:
    QList<QString> getQrCodes();
    int getQrCodesCount();
    QString getVpnKey();

    ErrorCode executeRequest(const QString &endpoint, const QJsonObject &apiPayload, QByteArray &responseBody, bool isTestPurchase = false);
    ErrorCode importServiceFromBilling(const QByteArray &responseBody, const bool isTestPurchase);

    bool importServiceForCountry(const QString &serverCountryCode, const ProtocolData &protocolData);

    QList<QString> m_qrCodes;
    QString m_vpnKey;

    QSharedPointer<ServersModel> m_serversModel;
    QSharedPointer<ApiServicesModel> m_apiServicesModel;
    std::shared_ptr<Settings> m_settings;

    QString m_subscriptionId;
    QString m_selectedServerCountryCode;
    bool m_importAllCountries = false;

    QJsonArray m_subscriptionConfigs;
};

#endif // APICONFIGSCONTROLLER_H
