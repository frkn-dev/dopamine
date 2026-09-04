#include <QDebug>
#include <QTimer>

#include "dopamine_application.h"
#include "core/osSignalHandler.h"
#include "migrations.h"
#include "version.h"

#include <QTimer>

#ifdef Q_OS_WIN
    #include "Windows.h"
#endif

#if defined(Q_OS_IOS)
    #include "platforms/ios/QtAppDelegate-C-Interface.h"
#endif

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
bool isAnotherInstanceRunning(const QString &dataToForward)
{
    QLocalSocket socket;
    socket.connectToServer("DopamineVPNInstance");
    if (socket.waitForConnected(500)) {
        qWarning() << "Client is already running";
        if (!dataToForward.isEmpty()) {
            socket.write(dataToForward.toUtf8());
            socket.waitForBytesWritten(1000);
        }
        socket.disconnectFromServer();
        return true;
    }
    return false;
}
#endif

int main(int argc, char *argv[])
{
    Migrations migrationsManager;
    migrationsManager.doMigrations();

#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif

#ifdef Q_OS_ANDROID
    // QTBUG-95974 QTBUG-95764 QTBUG-102168
    qputenv("QT_ANDROID_DISABLE_ACCESSIBILITY", "1");
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
#endif

    DopamineApplication app(argc, argv);
    OsSignalHandler::setup();

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    // Check for URL or import data in arguments (e.g. frkn://... passed by OS)
    QString dataToForward;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg.startsWith("frkn://") || arg.startsWith("https://") || arg.startsWith("http://")) {
            dataToForward = arg;
            break;
        }
    }
    if (isAnotherInstanceRunning(dataToForward)) {
        QTimer::singleShot(1000, &app, [&]() { app.quit(); });
        return app.exec();
    }
    app.startLocalServer();
#endif

// Allow to raise app window if secondary instance launched
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(0);
#endif

    app.registerTypes();

    app.setApplicationName(APPLICATION_NAME);
    app.setOrganizationName(ORGANIZATION_NAME);
    app.setApplicationDisplayName(APPLICATION_NAME);

    app.loadFonts();

    bool doExec = app.parseCommands();

    if (doExec) {
        app.init();

        qInfo().noquote() << QString("Started %1 version %2 %3").arg(APPLICATION_NAME, APP_VERSION, GIT_COMMIT_HASH);
        qInfo().noquote() << QString("%1 (%2)").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
        qInfo().noquote() << QString("SSL backend: %1").arg(QSslSocket::sslLibraryVersionString());

        return app.exec();
    }
    return 0;
}
