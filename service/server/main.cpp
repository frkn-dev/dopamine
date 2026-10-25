#include <QDir>

#include "version.h"
#include "localserver.h"
#include "logger.h"
#include "systemservice.h"
#include "utilities.h"

#ifdef Q_OS_WIN
#include "platforms/windows/daemon/windowsdaemontunnel.h"

namespace {
int s_argc = 0;
char** s_argv = nullptr;
}  // namespace

#endif

int runApplication(int argc, char** argv)
{
    QCoreApplication app(argc,argv);

#ifdef Q_OS_WIN
    if(argc > 2){
        s_argc = argc;
        s_argv = argv;
        QStringList tokens;
        for (int i = 1; i < argc; ++i) {
            tokens.append(QString(argv[i]));
        }

        if (!tokens.empty() && tokens[0] == "tunneldaemon") {
            // The tunnel process is a per-connection worker for tunnel.dll, not
            // a daemon host. Falling through to LocalServer below gave it its
            // own WindowsDaemon - a second firewall engine, a second listener on
            // \\.\pipe\frkn and, worse, a second opener of the split-tunnel
            // device: initDriver() sees an initialized driver, resets it and
            // wipes the rules the real service had just pushed, and from then on
            // the two processes fight over the device, which only one may hold.
            // That is the "Failed to open Driver: access denied" that left
            // split tunneling dead.
            WindowsDaemonTunnel daemon;
            return daemon.run(tokens);
        }
    }
#endif

    LocalServer localServer;
    return app.exec();

}


int main(int argc, char **argv)
{
    Utils::initializePath(Logger::systemLogDir());

    // Log to file from birth: otherwise (ipcserver setLogsEnabled) nothing is
    // captured until a client connects over IPC — daemon init, driver install
    // and early failures stay invisible exactly when we need them most.
    Logger::init(true);

    if (argc >= 2) {
        qInfo() << "Started as console application";
        return runApplication(argc, argv);
    }
    else {
        qInfo() << "Started as system service";
#ifdef Q_OS_WIN
        SystemService systemService(argc, argv);
        return systemService.exec();
#else
    return runApplication(argc, argv);
#endif

    }
}
