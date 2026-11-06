#ifndef SETTINGSCONTROLLER_H
#define SETTINGSCONTROLLER_H

#include <QObject>

#include "ui/models/containers_model.h"
#include "ui/models/languageModel.h"
#include "ui/models/servers_model.h"
#include "ui/models/sites_model.h"
#include "ui/models/appSplitTunnelingModel.h"

class SettingsController : public QObject
{
    Q_OBJECT
public:
    explicit SettingsController(const QSharedPointer<ServersModel> &serversModel,
                                const QSharedPointer<ContainersModel> &containersModel,
                                const QSharedPointer<LanguageModel> &languageModel,
                                const QSharedPointer<SitesModel> &sitesModel,
                                const QSharedPointer<AppSplitTunnelingModel> &appSplitTunnelingModel,
                                const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);

    Q_PROPERTY(QString primaryDns READ getPrimaryDns WRITE setPrimaryDns NOTIFY primaryDnsChanged)
    Q_PROPERTY(QString secondaryDns READ getSecondaryDns WRITE setSecondaryDns NOTIFY secondaryDnsChanged)
    Q_PROPERTY(bool dnsOverrideEnabled READ isDnsOverrideEnabled WRITE setDnsOverrideEnabled NOTIFY dnsOverrideEnabledChanged)
    Q_PROPERTY(bool isLoggingEnabled READ isLoggingEnabled WRITE toggleLogging NOTIFY loggingStateChanged)
    Q_PROPERTY(bool isTelemetryEnabled READ isTelemetryEnabled WRITE setTelemetryEnabled NOTIFY telemetryEnabledChanged)
    Q_PROPERTY(bool isExperimentalFeaturesEnabled READ isExperimentalFeaturesEnabled
               WRITE setExperimentalFeaturesEnabled NOTIFY experimentalFeaturesChanged)
    // спрашивали ли уже про телеметрию (одноразовый вопрос после первого коннекта)
    Q_PROPERTY(bool isTelemetryPromptShown READ isTelemetryPromptShown WRITE setTelemetryPromptShown
               NOTIFY telemetryPromptShownChanged)
    Q_PROPERTY(bool isReconnectUntilCancelled READ isReconnectUntilCancelled WRITE setReconnectUntilCancelled NOTIFY reconnectUntilCancelledChanged)
    Q_PROPERTY(bool isPostConnectCheckEnabled READ isPostConnectCheckEnabled WRITE setPostConnectCheckEnabled NOTIFY postConnectCheckChanged)
    Q_PROPERTY(int mtuOverride READ mtuOverride WRITE setMtuOverride NOTIFY mtuOverrideChanged)
    // MTU, с которым реально подняли туннель в последний раз (0 = ещё не подключались)
    Q_PROPERTY(int lastAppliedMtu READ lastAppliedMtu NOTIFY lastAppliedMtuChanged)
    Q_PROPERTY(bool isEndpointTogglesAllowed READ isEndpointTogglesAllowed WRITE setEndpointTogglesAllowed NOTIFY endpointTogglesAllowedChanged)
    // Адреса пула, выключенные пользователем. Обычное свойство, а не вызов
    // метода: QML-биндинг сам обновляет карточку, и не зависит от того,
    // попал ли новый Q_INVOKABLE в мета-объект конкретной сборки.
    Q_PROPERTY(QStringList poolDisabledIps READ poolDisabledIps WRITE setPoolDisabledIps NOTIFY poolDisabledIpsChanged)
    Q_PROPERTY(bool isServerPingTextVisible READ isServerPingTextVisible WRITE setServerPingTextVisible NOTIFY serverPingTextVisibleChanged)
    Q_PROPERTY(bool isNotificationPermissionGranted READ isNotificationPermissionGranted NOTIFY onNotificationStateChanged)
    Q_PROPERTY(bool isKillSwitchEnabled READ isKillSwitchEnabled WRITE toggleKillSwitch NOTIFY killSwitchEnabledChanged)
    Q_PROPERTY(bool isRouteLanThroughVpn READ isRouteLanThroughVpn WRITE toggleRouteLanThroughVpn NOTIFY routeLanThroughVpnChanged)
    Q_PROPERTY(bool strictKillSwitchEnabled READ isStrictKillSwitchEnabled WRITE toggleStrictKillSwitch NOTIFY strictKillSwitchEnabledChanged)
    Q_PROPERTY(bool autoServerListUpdateEnabled READ isAutoServerListUpdateEnabled WRITE toggleAutoServerListUpdate NOTIFY autoServerListUpdateEnabledChanged)

    Q_PROPERTY(bool isDevModeEnabled READ isDevModeEnabled NOTIFY devModeEnabled)
    Q_PROPERTY(QString gatewayEndpoint READ getGatewayEndpoint WRITE setGatewayEndpoint NOTIFY gatewayEndpointChanged)
    Q_PROPERTY(bool isDevGatewayEnv READ isDevGatewayEnv WRITE toggleDevGatewayEnv NOTIFY devGatewayEnvChanged)

    Q_PROPERTY(bool isHomeAdLabelVisible READ isHomeAdLabelVisible NOTIFY isHomeAdLabelVisibleChanged)
    Q_PROPERTY(bool startMinimized READ isStartMinimizedEnabled NOTIFY startMinimizedChanged)
    Q_PROPERTY(int safeAreaTopMargin READ getSafeAreaTopMargin NOTIFY safeAreaTopMarginChanged)
    Q_PROPERTY(int safeAreaBottomMargin READ getSafeAreaBottomMargin NOTIFY safeAreaBottomMarginChanged)
    Q_PROPERTY(int imeHeight READ getImeHeight NOTIFY imeHeightChanged)
    Q_PROPERTY(QString serversProtocolFilter READ getServersProtocolFilter WRITE setServersProtocolFilter NOTIFY serversProtocolFilterChanged)
    Q_PROPERTY(QString serversEnvFilter READ getServersEnvFilter WRITE setServersEnvFilter NOTIFY serversEnvFilterChanged)
    Q_PROPERTY(QString serversSubscriptionFilter READ getServersSubscriptionFilter WRITE setServersSubscriptionFilter NOTIFY serversSubscriptionFilterChanged)
    Q_PROPERTY(bool autoServerSelection READ isAutoServerSelection WRITE setAutoServerSelection NOTIFY autoServerSelectionChanged)

public slots:
    void toggleAmneziaDns(bool enable);
    bool isAmneziaDnsEnabled();

    QString getPrimaryDns();
    void setPrimaryDns(const QString &dns);

    QString getSecondaryDns();
    void setSecondaryDns(const QString &dns);

    bool isDnsOverrideEnabled();
    void setDnsOverrideEnabled(bool enabled);

    bool isLoggingEnabled();
    bool isTelemetryEnabled();
    void setTelemetryEnabled(bool enabled);
    bool isExperimentalFeaturesEnabled();
    void setExperimentalFeaturesEnabled(bool enabled);
    bool isTelemetryPromptShown();
    void setTelemetryPromptShown(bool shown);
    bool isReconnectUntilCancelled();
    bool isPostConnectCheckEnabled();
    void setPostConnectCheckEnabled(bool enabled);
    void setReconnectUntilCancelled(bool enabled);
    int mtuOverride();
    int lastAppliedMtu();
    bool isEndpointTogglesAllowed();
    void setEndpointTogglesAllowed(bool allowed);
    QStringList poolDisabledIps();
    void setPoolDisabledIps(const QStringList &ips);
    void setMtuOverride(int mtu);
    void toggleLogging(bool enable);

    bool isServerPingTextVisible();
    void setServerPingTextVisible(bool visible);

    bool isAutoServerSelection();
    void setAutoServerSelection(bool enabled);

    void openLogsFolder();
    void openServiceLogsFolder();
    void exportLogsFile(const QString &fileName);
    void exportServiceLogsFile(const QString &fileName);
    void clearLogs();

    void backupAppConfig(const QString &fileName);
    void restoreAppConfig(const QString &fileName);
    void restoreAppConfigFromData(const QByteArray &data);

    QString getAppVersion();

    void clearSettings();

    bool isAutoConnectEnabled();
    void toggleAutoConnect(bool enable);

    bool isAutoStartEnabled();
    void toggleAutoStart(bool enable);

    bool isStartMinimizedEnabled();
    void toggleStartMinimized(bool enable);

    bool isScreenshotsEnabled();
    void toggleScreenshotsEnabled(bool enable);

    bool isCameraPresent();

    bool isKillSwitchEnabled();
    void toggleKillSwitch(bool enable);

    bool isRouteLanThroughVpn();
    void toggleRouteLanThroughVpn(bool enable);

    bool isStrictKillSwitchEnabled();
    void toggleStrictKillSwitch(bool enable);

    bool isAutoServerListUpdateEnabled();
    void toggleAutoServerListUpdate(bool enable);

    bool isNotificationPermissionGranted();
    void requestNotificationPermission();

    QString getInstallationUuid();

    void enableDevMode();
    bool isDevModeEnabled();

    void resetGatewayEndpoint();
    void setGatewayEndpoint(const QString &endpoint);
    QString getGatewayEndpoint();
    bool isDevGatewayEnv();
    void toggleDevGatewayEnv(bool enabled);

    bool isOnTv();
    bool isEdgeToEdgeEnabled();
    int getStatusBarHeight();
    int getNavigationBarHeight();
    int getSafeAreaTopMargin();
    int getSafeAreaBottomMargin();
    int getImeHeight();

    bool isHomeAdLabelVisible();
    void disableHomeAdLabel();

    bool frknDarkMode();
    void toggleFrknDarkMode(bool enable);

    QString getServersProtocolFilter();
    void setServersProtocolFilter(const QString &filter);

    QString getServersEnvFilter();
    void setServersEnvFilter(const QString &filter);

    QString getServersSubscriptionFilter();
    void setServersSubscriptionFilter(const QString &filter);

signals:
    void primaryDnsChanged();
    void secondaryDnsChanged();
    void dnsOverrideEnabledChanged();
    void loggingStateChanged();
    void telemetryEnabledChanged();
    void telemetryPromptShownChanged();
    void experimentalFeaturesChanged();
    void reconnectUntilCancelledChanged();
    void postConnectCheckChanged();
    void mtuOverrideChanged();
    void lastAppliedMtuChanged();
    void endpointTogglesAllowedChanged();
    void poolDisabledIpsChanged();
    void serverPingTextVisibleChanged();
    void killSwitchEnabledChanged();
    void routeLanThroughVpnChanged();
    void strictKillSwitchEnabledChanged(bool enabled);
    void autoServerListUpdateEnabledChanged(bool enabled);

    void restoreBackupFinished();
    void changeSettingsFinished(const QString &finishedMessage);
    void changeSettingsErrorOccurred(const QString &errorMessage);

    void saveFile(const QString &fileName, const QString &data);

    void importBackupFromOutside(QString filePath);

    void amneziaDnsToggled(bool enable);

    void loggingDisableByWatcher();

    void onNotificationStateChanged();

    void devModeEnabled();
    void gatewayEndpointChanged(const QString &endpoint);
    void devGatewayEnvChanged(bool enabled);

    void imeHeightChanged(int height);
    void safeAreaTopMarginChanged();
    void safeAreaBottomMarginChanged();

    void isHomeAdLabelVisibleChanged(bool visible);
    void startMinimizedChanged();

    void serversProtocolFilterChanged();
    void serversEnvFilterChanged();
    void serversSubscriptionFilterChanged();
    void autoServerSelectionChanged();

private:
    QSharedPointer<ServersModel> m_serversModel;
    QSharedPointer<ContainersModel> m_containersModel;
    QSharedPointer<LanguageModel> m_languageModel;
    QSharedPointer<SitesModel> m_sitesModel;
    QSharedPointer<AppSplitTunnelingModel> m_appSplitTunnelingModel;

    mutable int m_cachedStatusBarHeight = -1;
    mutable int m_cachedNavigationBarHeight = -1;
    mutable bool m_cachedEdgeToEdgeEnabled = false;
    mutable bool m_edgeToEdgeCached = false;
    int m_imeHeight = 0;
    std::shared_ptr<Settings> m_settings;

    QString m_appVersion;

    QString getPlatform();

    QDateTime m_loggingDisableDate;

    bool m_isDevModeEnabled = false;

    void checkIfNeedDisableLogs();
};

#endif // SETTINGSCONTROLLER_H
