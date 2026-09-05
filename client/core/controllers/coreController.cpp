#include "coreController.h"

#include <QDirIterator>
#include <QTranslator>
#include <memory>

#if defined(Q_OS_ANDROID)
    #include "core/installedAppsImageProvider.h"
    #include "platforms/android/android_controller.h"
#endif

#if defined(Q_OS_IOS)
    #include "platforms/ios/ios_controller.h"
    #include <Dopamine-Swift.h>
#endif

namespace
{
    auto oneshot_sub_fetch(ImportController *ic, PageController *pc, CoreController *cc)
    {
        auto singleConn = std::make_shared<QMetaObject::Connection>();
        auto multiConn = std::make_shared<QMetaObject::Connection>();
        auto errorConn = std::make_shared<QMetaObject::Connection>();
        auto dupConn = std::make_shared<QMetaObject::Connection>();

        auto cleanup = [singleConn, multiConn, errorConn, dupConn]() {
            QObject::disconnect(*singleConn);
            QObject::disconnect(*multiConn);
            QObject::disconnect(*errorConn);
            QObject::disconnect(*dupConn);
        };

        *singleConn = cc->connect(ic, &ImportController::qrDecodingFinished, cc, [ic, pc, cleanup]() {
            cleanup();
            ic->queueConfigForConfirmation();
            emit pc->goToPageConfigSource();
        });
        *multiConn = cc->connect(ic, &ImportController::subscriptionConfigsReady, cc, [pc, cleanup](int) {
            cleanup();
            emit pc->goToPageConfigSource();
        });
        *errorConn = cc->connect(ic, &ImportController::subscriptionErrorOccurred, cc, [pc, cleanup](const QString &msg) {
            cleanup();
            emit pc->showErrorMessage(msg);
        });
        *dupConn = cc->connect(ic, &ImportController::subscriptionAllDuplicates, cc, [pc, cleanup]() {
            cleanup();
            emit pc->showNotificationMessage(QObject::tr("All configurations have already been added"));
        });
    }
}

CoreController::CoreController(const QSharedPointer<VpnConnection> &vpnConnection, const std::shared_ptr<Settings> &settings,
                               QQmlApplicationEngine *engine, QObject *parent)
    : QObject(parent), m_vpnConnection(vpnConnection), m_settings(settings), m_engine(engine)
{
    initModels();
    initControllers();
    initSignalHandlers();

    initAndroidController();
    initAppleController();

    initNotificationHandler();

    m_translator.reset(new QTranslator());
    updateTranslator(m_settings->getAppLanguage());
}

void CoreController::initModels()
{
    m_containersModel.reset(new ContainersModel(this));
    m_engine->rootContext()->setContextProperty("ContainersModel", m_containersModel.get());

    m_defaultServerContainersModel.reset(new ContainersModel(this));
    m_engine->rootContext()->setContextProperty("DefaultServerContainersModel", m_defaultServerContainersModel.get());

    m_serversModel.reset(new ServersModel(m_settings, this));
    m_engine->rootContext()->setContextProperty("ServersModel", m_serversModel.get());

    m_languageModel.reset(new LanguageModel(m_settings, this));
    m_engine->rootContext()->setContextProperty("LanguageModel", m_languageModel.get());

    m_sitesModel.reset(new SitesModel(m_settings, this));
    m_engine->rootContext()->setContextProperty("SitesModel", m_sitesModel.get());

    m_allowedDnsModel.reset(new AllowedDnsModel(m_settings, this));
    m_engine->rootContext()->setContextProperty("AllowedDnsModel", m_allowedDnsModel.get());

    m_appSplitTunnelingModel.reset(new AppSplitTunnelingModel(m_settings, this));
    m_engine->rootContext()->setContextProperty("AppSplitTunnelingModel", m_appSplitTunnelingModel.get());

    m_apiServicesModel.reset(new ApiServicesModel(this));
    m_engine->rootContext()->setContextProperty("ApiServicesModel", m_apiServicesModel.get());

    m_apiCountryModel.reset(new ApiCountryModel(this));
    m_engine->rootContext()->setContextProperty("ApiCountryModel", m_apiCountryModel.get());

    m_apiAccountInfoModel.reset(new ApiAccountInfoModel(this));
    m_engine->rootContext()->setContextProperty("ApiAccountInfoModel", m_apiAccountInfoModel.get());

    m_apiDevicesModel.reset(new ApiDevicesModel(m_settings, this));
    m_engine->rootContext()->setContextProperty("ApiDevicesModel", m_apiDevicesModel.get());
}

void CoreController::initControllers()
{
    m_connectionController.reset(
            new ConnectionController(m_serversModel, m_containersModel, m_vpnConnection, m_settings));
    m_engine->rootContext()->setContextProperty("ConnectionController", m_connectionController.get());

    m_pageController.reset(new PageController(m_serversModel, m_settings));
    m_engine->rootContext()->setContextProperty("PageController", m_pageController.get());

    m_focusController.reset(new FocusController(m_engine, this));
    m_engine->rootContext()->setContextProperty("FocusController", m_focusController.get());

    m_installController.reset(new InstallController(m_serversModel, m_settings));
    m_engine->rootContext()->setContextProperty("InstallController", m_installController.get());

    m_importController.reset(new ImportController(m_serversModel, m_containersModel, m_settings));
    m_engine->rootContext()->setContextProperty("ImportController", m_importController.get());

    m_settingsController.reset(
            new SettingsController(m_serversModel, m_containersModel, m_languageModel, m_sitesModel, m_appSplitTunnelingModel, m_settings));
    m_engine->rootContext()->setContextProperty("SettingsController", m_settingsController.get());

    m_sitesController.reset(new SitesController(m_settings, m_sitesModel));
    m_engine->rootContext()->setContextProperty("SitesController", m_sitesController.get());

    m_allowedDnsController.reset(new AllowedDnsController(m_settings, m_allowedDnsModel));
    m_engine->rootContext()->setContextProperty("AllowedDnsController", m_allowedDnsController.get());

    m_appSplitTunnelingController.reset(new AppSplitTunnelingController(m_settings, m_appSplitTunnelingModel));
    m_engine->rootContext()->setContextProperty("AppSplitTunnelingController", m_appSplitTunnelingController.get());

    m_systemController.reset(new SystemController(m_settings));
    m_engine->rootContext()->setContextProperty("SystemController", m_systemController.get());

    m_apiSettingsController.reset(
            new ApiSettingsController(m_serversModel, m_apiAccountInfoModel, m_apiCountryModel, m_apiDevicesModel, m_settings));
    m_engine->rootContext()->setContextProperty("ApiSettingsController", m_apiSettingsController.get());

    m_apiConfigsController.reset(new ApiConfigsController(m_serversModel, m_apiServicesModel, m_settings));
    m_engine->rootContext()->setContextProperty("ApiConfigsController", m_apiConfigsController.get());
    m_connectionController->setApiConfigsController(m_apiConfigsController.get());

    connect(m_importController.get(), &ImportController::frknSubscriptionLinkDetected, this,
            [this](const QString &subscriptionId) {
                qDebug() << "[CORE] frkn subscription link detected:" << subscriptionId;
                m_pageController->showBusyIndicator(true);
                bool ok = m_apiConfigsController->fetchSubscriptionConfigs(subscriptionId);
                m_pageController->showBusyIndicator(false);
                qDebug() << "[CORE] fetch subscription configs result:" << ok;
                if (ok) {
                    emit m_pageController->goToPage(PageLoader::PageEnum::PageSetupWizardSubscriptionProtocols);
                    qDebug() << "[CORE] navigated to subscription protocols page";
                }
            });

    connect(m_importController.get(), &ImportController::frknShareLinkDetected, this,
            [this](const QString &shareToken) {
                qDebug() << "[CORE] frkn share link detected";
                m_pageController->showBusyIndicator(true);
                m_apiConfigsController->importSharedConnection(shareToken);
                m_pageController->showBusyIndicator(false);
            });

    m_splitPresetsModel.reset(new SplitPresetsModel(m_settings, m_serversModel, this));
    m_engine->rootContext()->setContextProperty("SplitPresetsModel", m_splitPresetsModel.get());

    m_healthCheckController.reset(new HealthCheckController(m_serversModel, this));
    m_engine->rootContext()->setContextProperty("HealthCheckController", m_healthCheckController.get());
    m_connectionController->setHealthCheckController(m_healthCheckController.get());

    // probes are only meaningful with the VPN off — once a tunnel comes up, in-flight
    // probes die (their traffic routes into the tunnel) and stale "offline" badges
    // would linger next to the very server we are connected to
    connect(m_vpnConnection.get(), &VpnConnection::connectionStateChanged, this, [this](Vpn::ConnectionState state) {
        if (state == Vpn::ConnectionState::Connecting || state == Vpn::ConnectionState::Connected) {
            m_healthCheckController->stopProbe();
            m_serversModel->clearHealthResults();
        }
    });
}

void CoreController::initAndroidController()
{
#ifdef Q_OS_ANDROID
    if (!AndroidController::initLogging()) {
        qFatal("Android logging initialization failed");
    }
    AndroidController::instance()->setSaveLogs(m_settings->isSaveLogs());
    connect(m_settings.get(), &Settings::saveLogsChanged, AndroidController::instance(), &AndroidController::setSaveLogs);

    AndroidController::instance()->setScreenshotsEnabled(m_settings->isScreenshotsEnabled());
    connect(m_settings.get(), &Settings::screenshotsEnabledChanged, AndroidController::instance(), &AndroidController::setScreenshotsEnabled);

    connect(m_settings.get(), &Settings::serverRemoved, AndroidController::instance(), &AndroidController::resetLastServer);

    connect(m_settings.get(), &Settings::settingsCleared, []() { AndroidController::instance()->resetLastServer(-1); });

    connect(AndroidController::instance(), &AndroidController::initConnectionState, this, [this](Vpn::ConnectionState state) {
        m_connectionController->onConnectionStateChanged(state);
        if (m_vpnConnection)
            m_vpnConnection->restoreConnection();
    });

    connect(AndroidController::instance(), &AndroidController::shakeDetected, m_pageController.get(), &PageController::shakeDetected);
    if (!AndroidController::instance()->initialize()) {
        qFatal("Android controller initialization failed");
    }

    connect(AndroidController::instance(), &AndroidController::importConfigFromOutside, this, [this](QString data) {
        emit m_pageController->goToPageHome();
        if (m_importController->extractConfigFromData(data)) {
            emit m_pageController->goToPageViewConfig();
        } else {
            oneshot_sub_fetch(m_importController.get(), m_pageController.get(), this);
        }
    });

    m_engine->addImageProvider(QLatin1String("installedAppImage"), new InstalledAppsImageProvider);
#endif
}

void CoreController::initAppleController()
{
#ifdef Q_OS_IOS
    IosController::Instance()->initialize();
    connect(IosController::Instance(), &IosController::importConfigFromOutside, this, [this](QString data) {
        emit m_pageController->goToPageHome();
        if (m_importController->extractConfigFromData(data)) {
            emit m_pageController->goToPageViewConfig();
        } else {
            oneshot_sub_fetch(m_importController.get(), m_pageController.get(), this);
        }
    });

    connect(IosController::Instance(), &IosController::shakeDetected, m_pageController.get(), &PageController::shakeDetected);

    connect(IosController::Instance(), &IosController::importBackupFromOutside, this, [this](QString filePath) {
        emit m_pageController->goToPageHome();
        m_pageController->goToPageSettingsBackup();
        emit m_settingsController->importBackupFromOutside(filePath);
    });

    QTimer::singleShot(0, this, [this]() { Dopamine::toggleScreenshots(m_settings->isScreenshotsEnabled()); });

    connect(m_settings.get(), &Settings::screenshotsEnabledChanged, [](bool enabled) { Dopamine::toggleScreenshots(enabled); });
#endif
}

void CoreController::initSignalHandlers()
{
    initErrorMessagesHandler();

    initApiCountryModelUpdateHandler();
    initContainerModelUpdateHandler();
    initTranslationsUpdatedHandler();
    initAutoConnectHandler();
    initAmneziaDnsToggledHandler();
    initPrepareConfigHandler();
    initStrictKillSwitchHandler();
}

void CoreController::initNotificationHandler()
{
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    m_notificationHandler.reset(NotificationHandler::create(nullptr));

    connect(m_vpnConnection.get(), &VpnConnection::connectionStateChanged, m_notificationHandler.get(),
            &NotificationHandler::setConnectionState);

    connect(m_notificationHandler.get(), &NotificationHandler::raiseRequested, m_pageController.get(), &PageController::raiseMainWindow);
    connect(m_notificationHandler.get(), &NotificationHandler::connectRequested, m_connectionController.get(),
            static_cast<void (ConnectionController::*)()>(&ConnectionController::openConnection));
    connect(m_notificationHandler.get(), &NotificationHandler::disconnectRequested, m_connectionController.get(),
            &ConnectionController::closeConnection);
    connect(this, &CoreController::translationsUpdated, m_notificationHandler.get(), &NotificationHandler::onTranslationsUpdated);

    auto* trayHandler = qobject_cast<SystemTrayNotificationHandler*>(m_notificationHandler.get());
    connect(this, &CoreController::websiteUrlChanged, trayHandler, &SystemTrayNotificationHandler::updateWebsiteUrl);

    // keep the tray menu's info item in sync with the currently selected server
    auto pushServerName = [this, trayHandler]() {
        trayHandler->setServerName(
                m_serversModel->data(m_serversModel->getDefaultServerIndex(), ServersModel::Roles::NameRole).toString());
    };
    connect(m_connectionController.get(), &ConnectionController::connectionStateChanged, trayHandler, pushServerName);
    connect(m_serversModel.get(), &ServersModel::defaultServerIndexChanged, trayHandler, pushServerName);
    pushServerName();
#endif
}

void CoreController::updateTranslator(const QLocale &locale)
{
    if (!m_translator->isEmpty()) {
        QCoreApplication::removeTranslator(m_translator.get());
    }

    QStringList availableTranslations;
    QDirIterator it(":/translations", QStringList("dopamine_*.qm"), QDir::Files);
    while (it.hasNext()) {
        availableTranslations << it.next();
    }

    // This code allow to load translation for the language only, without country code
    const QString lang = locale.name().split("_").first();
    const QString translationFilePrefix = QString(":/translations/dopamine_") + lang;
    QString strFileName = QString(":/translations/dopamine_%1.qm").arg(locale.name());
    for (const QString &translation : availableTranslations) {
        if (translation.contains(translationFilePrefix)) {
            strFileName = translation;
            break;
        }
    }

    if (m_translator->load(strFileName)) {
        if (QCoreApplication::installTranslator(m_translator.get())) {
            m_settings->setAppLanguage(locale);
        }
    } else {
        m_settings->setAppLanguage(QLocale::English);
    }

    m_engine->retranslate();

    emit translationsUpdated();
    emit websiteUrlChanged(m_languageModel->getCurrentSiteUrl());
}

void CoreController::initErrorMessagesHandler()
{
    connect(m_connectionController.get(), &ConnectionController::connectionErrorOccurred, this, [this](ErrorCode errorCode) {
        emit m_pageController->showErrorMessage(errorCode);
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Disconnected);
    });

    connect(m_apiConfigsController.get(), &ApiConfigsController::errorOccurred, m_pageController.get(),
            qOverload<ErrorCode>(&PageController::showErrorMessage));
}

void CoreController::setQmlRoot()
{
    m_systemController->setQmlRoot(m_engine->rootObjects().value(0));
}

void CoreController::initApiCountryModelUpdateHandler()
{
    connect(m_serversModel.get(), &ServersModel::updateApiCountryModel, this, [this]() {
        m_apiCountryModel->updateModel(m_serversModel->getProcessedServerData("apiAvailableCountries").toJsonArray(),
                                       m_serversModel->getProcessedServerData("apiServerCountryCode").toString());
    });
}

void CoreController::initContainerModelUpdateHandler()
{
    connect(m_serversModel.get(), &ServersModel::containersUpdated, m_containersModel.get(), &ContainersModel::updateModel);
    connect(m_serversModel.get(), &ServersModel::defaultServerContainersUpdated, m_defaultServerContainersModel.get(),
            &ContainersModel::updateModel);
    connect(m_serversModel.get(), &ServersModel::gatewayStacksExpanded, this, [this]() {
        m_splitPresetsModel->fetchPresets();
    });
    // warm the account_info cache on start, so opening the server card
    // doesn't do an API round-trip (served from cache for the next hour).
    // Skip while no default server is selected yet (first import in progress):
    // hasServersFromGatewayApiChanged fires again as soon as one appears.
    auto warmAccountInfoCache = [this]() {
        const int defaultIndex = m_serversModel->getDefaultServerIndex();
        if (m_serversModel->hasServersFromGatewayApi() && defaultIndex >= 0) {
            m_serversModel->setProcessedServerIndex(defaultIndex);
            m_apiSettingsController->getAccountInfo(true);
        }
    };
    connect(m_serversModel.get(), &ServersModel::hasServersFromGatewayApiChanged, this, warmAccountInfoCache);
    QTimer::singleShot(0, this, warmAccountInfoCache);
    m_serversModel->resetModel();
    // the presets catalog is public — fetch on every app start
    m_splitPresetsModel->fetchPresets();
    // pick up backend-side config changes (e.g. node IP updates) — throttled inside
    m_apiConfigsController->refreshSubscriptionConfigs();
}

void CoreController::initTranslationsUpdatedHandler()
{
    connect(m_languageModel.get(), &LanguageModel::updateTranslations, this, &CoreController::updateTranslator);
    connect(this, &CoreController::translationsUpdated, m_languageModel.get(), &LanguageModel::translationsUpdated);
    connect(this, &CoreController::translationsUpdated, m_connectionController.get(), &ConnectionController::onTranslationsUpdated);
}

void CoreController::initAutoConnectHandler()
{
    if (m_settingsController->isAutoConnectEnabled() && m_serversModel->getDefaultServerIndex() >= 0) {
        QTimer::singleShot(1000, this, [this]() { m_connectionController->openConnection(); });
    }
}

void CoreController::initAmneziaDnsToggledHandler()
{
    connect(m_settingsController.get(), &SettingsController::amneziaDnsToggled, m_serversModel.get(), &ServersModel::toggleAmneziaDns);
}

void CoreController::initPrepareConfigHandler()
{
    connect(m_connectionController.get(), &ConnectionController::prepareConfig, this, [this]() {
        emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Preparing);

        // auto-select picks the server itself — validating the UI-selected default
        // server here would block auto mode whenever that server was never connected
        if (m_settings->isAutoServerSelection()) {
            m_connectionController->openConnection();
            return;
        }

        if (!m_apiConfigsController->isConfigValid()) {
            emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Disconnected);
            return;
        }

        m_installController->validateConfig();
    });

    connect(m_installController.get(), &InstallController::configValidated, this, [this](bool isValid) {
        if (!isValid) {
            emit m_vpnConnection->connectionStateChanged(Vpn::ConnectionState::Disconnected);
            return;
        }

        m_connectionController->openConnection();
    });
}

void CoreController::initStrictKillSwitchHandler()
{
    connect(m_settingsController.get(), &SettingsController::strictKillSwitchEnabledChanged, m_vpnConnection.get(),
            &VpnConnection::onKillSwitchModeChanged);
}

QSharedPointer<PageController> CoreController::pageController() const
{
    return m_pageController;
}

void CoreController::openConnectionByIndex(int serverIndex)
{
    if (m_serversModel) {
        m_serversModel->setProcessedServerIndex(serverIndex);
        m_serversModel->setDefaultServerIndex(serverIndex);
    }
    m_connectionController->toggleConnection();
}

void CoreController::importConfigFromData(const QString &data)
{
    if (!m_importController)
        return;

    if (m_importController->extractConfigFromData(data)) {
        m_importController->queueConfigForConfirmation();
        emit m_pageController->goToPageConfigSource();
    } else {
        oneshot_sub_fetch(m_importController.get(), m_pageController.get(), this);
    }
}
