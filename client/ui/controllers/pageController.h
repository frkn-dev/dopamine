#ifndef PAGECONTROLLER_H
#define PAGECONTROLLER_H

#include <QObject>
#include <QQmlEngine>

#include "core/defs.h"
#include "ui/models/servers_model.h"

namespace PageLoader
{
    Q_NAMESPACE
    enum class PageEnum {
        PageStart = 0,
        PageHome,

        PageSettingsServersList,
        PageSettings,
        PageSettingsServerData,
        PageSettingsServerInfo,
        PageSettingsConnection,
        PageSettingsDns,
        PageSettingsApplication,
        PageSettingsBackup,
        PageSettingsAbout,
        PageSettingsLogging,
        PageSettingsSplitTunneling,
        PageSettingsSplitPresets,
        PageSettingsAppSplitTunneling,
        PageSettingsKillSwitch,
        PageSettingsApiServerInfo,
        PageSettingsApiAvailableCountries,
        PageSettingsApiSupport,
        PageSettingsApiInstructions,
        PageSettingsApiNativeConfigs,
        PageSettingsApiDevices,
        PageSettingsApiSubscriptionKey,
        PageSettingsKillSwitchExceptions,

        PageSetupWizardStart,
        PageSetupWizardConfigSource,
        PageSetupWizardViewConfig,
        PageSetupWizardQrReader,
        PageSetupWizardApiServicesList,
        PageSetupWizardApiServiceInfo,
        PageSetupWizardSubscriptionProtocols,

        PageDevMenu
    };
    Q_ENUM_NS(PageEnum)

    static void declareQmlPageEnum()
    {
        qmlRegisterUncreatableMetaObject(PageLoader::staticMetaObject, "PageEnum", 1, 0, "PageEnum", "Error: only enums");
    }
}

class PageController : public QObject
{
    Q_OBJECT
public:
    explicit PageController(const QSharedPointer<ServersModel> &serversModel, const std::shared_ptr<Settings> &settings,
                            QObject *parent = nullptr);

public slots:
    bool isStartPageVisible();
    QString getPagePath(PageLoader::PageEnum page);

    void closeWindow();
    void hideWindow();
    void keyPressEvent(Qt::Key key);

    unsigned int getInitialPageNavigationBarColor();
    void updateNavigationBarColor(const int color);

    void showOnStartup();

    bool isTriggeredByConnectButton();
    void setTriggeredByConnectButton(bool trigger);

    void closeApplication();

    void setDrawerDepth(const int depth);
    int getDrawerDepth() const;
    int incrementDrawerDepth();
    int decrementDrawerDepth();

private slots:
    void onShowErrorMessage(amnezia::ErrorCode errorCode);

signals:
    void goToPage(PageLoader::PageEnum page, bool slide = true);
    void goToStartPage();
    void goToPageHome();
    void goToPageSettings();
    void goToPageViewConfig();
    void goToPageConfigSource();
    void goToPageSettingsBackup();

    void closePage();

    void restorePageHomeState(bool isContainerInstalled = false);

    void showErrorMessage(amnezia::ErrorCode);
    void showErrorMessage(const QString &errorMessage);
    void showNotificationMessage(const QString &message);

    void showBusyIndicator(bool visible);
    void disableControls(bool disabled);
    void disableTabBar(bool disabled);

    void hideMainWindow();
    void raiseMainWindow();

    void escapePressed();
    void closeTopDrawer();

    // forwarded from the platform shake-gesture hook (iOS, Android)
    void shakeDetected();

private:
    QSharedPointer<ServersModel> m_serversModel;

    std::shared_ptr<Settings> m_settings;

    bool m_isTriggeredByConnectButton;

    int m_drawerDepth = 0;
};

#endif // PAGECONTROLLER_H
