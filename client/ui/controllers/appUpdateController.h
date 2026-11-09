#ifndef APPUPDATECONTROLLER_H
#define APPUPDATECONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>

class ConnectionController;
class LanguageModel;
class QFile;
class QNetworkReply;
class QTimer;
class QCryptographicHash;

// Desktop self-update. The client downloads the same installer that is published
// on frkn.org (MSI / PKG / Linux .bin), checks sha256, disconnects the tunnel
// and hands the file to the OS installer. The manifest is
// https://frkn.org/dopamine/updates.json
namespace AppUpdate
{
    Q_NAMESPACE
    enum State {
        Idle = 0,
        Checking,
        UpToDate,
        UpdateAvailable,
        Downloading,
        Installing,
        Error
    };
    Q_ENUM_NS(State)

    inline void declareQmlEnum()
    {
        qmlRegisterUncreatableMetaObject(AppUpdate::staticMetaObject, "AppUpdate", 1, 0, "AppUpdate",
                                         QStringLiteral("Enums only"));
    }
}

class AppUpdateController : public QObject
{
    Q_OBJECT

public:
    explicit AppUpdateController(ConnectionController *connection, LanguageModel *language, QObject *parent = nullptr);
    ~AppUpdateController() override;

    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    Q_PROPERTY(AppUpdate::State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseNotesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

    bool isSupported() const;
    AppUpdate::State state() const { return m_state; }
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString errorMessage() const { return m_errorMessage; }
    double progress() const { return m_progress; }

public slots:
    void checkForUpdates();
    void downloadAndInstall();

signals:
    void stateChanged();
    void latestVersionChanged();
    void releaseNotesChanged();
    void errorMessageChanged();
    void progressChanged();

private slots:
    void onCheckFinished();
    void onDownloadFinished();
    void onConnectionStateChanged();
    void onDisconnectTimeout();
    void launchAndQuit();

private:
    void startCheck(bool interactive);
    void disconnectThenInstall();
    void fail(const QString &message);
    void resetTransfer(bool removePackage);
    void setState(AppUpdate::State state);
    void setProgress(double progress);
    QString notesFromManifest(const QJsonObject &root) const;
    bool startInstaller(const QString &path) const;

    ConnectionController *m_connection = nullptr;
    LanguageModel *m_language = nullptr;

    AppUpdate::State m_state = AppUpdate::Idle;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_errorMessage;
    double m_progress = 0;

    QString m_packageUrl;
    QByteArray m_expectedSha256;
    QString m_packagePath;

    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QCryptographicHash *m_hash = nullptr;
    QTimer *m_stallTimer = nullptr;
    QTimer *m_downloadTimer = nullptr;
    QTimer *m_disconnectTimer = nullptr;

    bool m_checkInteractive = false;
    bool m_waitingForDisconnect = false;
    bool m_launchStarted = false;
};

#endif // APPUPDATECONTROLLER_H
