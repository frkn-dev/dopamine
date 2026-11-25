#ifndef PAGECONTROLLER_H
#define PAGECONTROLLER_H

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

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
        PageSettingsSplitTunnelingOverview,
        PageSettingsSplitPresets,
        PageSettingsAppSplitTunneling,
        PageSettingsKillSwitch,
        PageSettingsApiServerInfo,
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
        // unused: subscription import no longer shows a protocol selection
        // screen (all configs are imported at once) — value kept so the enum
        // numbering stays stable
        PageSetupWizardSubscriptionProtocols,

        PageSettingsRuAppsHelp,

        PageDevMenu,

        PageSettingsShare,

        PageSettingsLocalSubnets,

        PageServerTest,
        PageServerTestInfo,

        // Settings → Subscriptions (appended: existing values stay stable)
        PageSettingsSubscriptions,

        // Settings → How it works (appended: existing values stay stable)
        PageHowItWorks
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
    // Successful manual reload: QML flies the mascot once (no extra bubble).
    void notifyServersLoaded();
    bool isStartPageVisible();
    QString getPagePath(PageLoader::PageEnum page);

    // Reference-counted busy indicator: several loaders can be busy at once
    // (legacy configs + peer cards, import flows, etc). Plain
    // showBusyIndicator(true/false) from each of them made the pterodactyl fly
    // once per pair; with a counter it flies once and lands when the last part
    // is done.
    void busyOn();
    void busyOff();

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

    // peer-pool cards of a FRKN subscription landed; QML flies the pterodactyl
    // in with a "Servers added: N" banner
    void showServersAddedBanner(int count);


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

    // Busy indicator reference count (see busyOn/busyOff).
    int m_busyRefs = 0;
    // Safety net for the refcount: a loader that never reports back used to
    // leave the mascot spinning forever, long after the work visibly finished.
    QTimer *m_busyWatchdog = nullptr;

    std::shared_ptr<Settings> m_settings;

    bool m_isTriggeredByConnectButton;

    int m_drawerDepth = 0;
};

#endif // PAGECONTROLLER_H
