#include "appUpdateController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "dopamine_application.h"
#include "ui/controllers/connectionController.h"
#include "ui/models/languageModel.h"
#include "version.h"

#if defined(Q_OS_WIN)
bool windowsPackageIsTrusted(const QString &path);
#endif

namespace {

constexpr qint64 kMaxPackageBytes = 512LL * 1024 * 1024;
constexpr int kMaxManifestBytes = 256 * 1024;
constexpr int kStallTimeoutMs = 45000;
constexpr int kDownloadTimeoutMs = 15 * 60 * 1000;
constexpr int kDisconnectTimeoutMs = 20000;

const QUrl kManifestUrl(QStringLiteral("https://frkn.org/dopamine/updates.json"));

bool isTrustedHttps(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        return false;
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        return false;
    }
    const QString host = url.host().toLower();
    return host == QLatin1String("frkn.org") || host.endsWith(QLatin1String(".frkn.org"));
}

QString platformKey()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_WIN)
    if (arch == QLatin1String("x86_64")) {
        return QStringLiteral("windows-x64");
    }
#elif defined(Q_OS_MACOS)
    if (arch == QLatin1String("arm64")) {
        return QStringLiteral("macos-arm64");
    }
    if (arch == QLatin1String("x86_64")) {
        return QStringLiteral("macos-x86_64");
    }
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (arch == QLatin1String("x86_64")) {
        return QStringLiteral("linux-x64");
    }
#else
    Q_UNUSED(arch);
#endif
    return {};
}

bool acceptedPackageName(const QString &name)
{
#if defined(Q_OS_WIN)
    return name.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
    return name.endsWith(QLatin1String(".pkg"), Qt::CaseInsensitive);
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return name.endsWith(QLatin1String(".bin"), Qt::CaseInsensitive)
            || name.endsWith(QLatin1String(".run"), Qt::CaseInsensitive);
#else
    Q_UNUSED(name);
    return false;
#endif
}

QString safeFileName(const QUrl &url)
{
    const QString name = url.fileName();
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9._-]{1,180}$"));
    if (!allowed.match(name).hasMatch()) {
        return {};
    }
    return name;
}

QByteArray parseSha256(const QString &text)
{
    const QString hex = text.trimmed().toLower();
    if (hex.size() != 64) {
        return {};
    }
    for (const QChar &c : hex) {
        const bool digit = c.isDigit();
        const bool hexLetter = c >= QLatin1Char('a') && c <= QLatin1Char('f');
        if (!digit && !hexLetter) {
            return {};
        }
    }
    return QByteArray::fromHex(hex.toLatin1());
}

int compareVersions(const QString &left, const QString &right)
{
    const auto parse = [](const QString &raw) {
        QVector<int> parts;
        const auto bits = raw.split(QLatin1Char('.'));
        for (const QString &bit : bits) {
            bool ok = false;
            const int n = bit.toInt(&ok);
            parts.append(ok ? n : 0);
        }
        return parts;
    };

    const QVector<int> a = parse(left);
    const QVector<int> b = parse(right);
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const int av = i < a.size() ? a.at(i) : 0;
        const int bv = i < b.size() ? b.at(i) : 0;
        if (av != bv) {
            return av < bv ? -1 : 1;
        }
    }
    return 0;
}

bool isTrustedPackage(const QString &path)
{
#if defined(Q_OS_WIN)
    return windowsPackageIsTrusted(path);
#elif defined(Q_OS_MACOS)
    // PKG is signed with "Developer ID Installer: FRKN LLP".
    QProcess proc;
    proc.start(QStringLiteral("pkgutil"), { QStringLiteral("--check-signature"), path });
    if (!proc.waitForFinished(10000)) {
        proc.kill();
        return false;
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    return proc.exitCode() == 0 && out.contains(QStringLiteral("FRKN LLP"));
#else
    // Linux installer is not separately signed; the manifest sha256 is the check.
    Q_UNUSED(path);
    return true;
#endif
}

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Dopamine/") + QStringLiteral(APP_VERSION));
    return request;
}

} // namespace

AppUpdateController::AppUpdateController(ConnectionController *connection, LanguageModel *language, QObject *parent)
    : QObject(parent), m_connection(connection), m_language(language)
{
    m_stallTimer = new QTimer(this);
    m_stallTimer->setSingleShot(true);
    connect(m_stallTimer, &QTimer::timeout, this, [this]() {
        qWarning() << "[UPDATE] download stalled";
        fail(tr("Could not download the update."));
    });

    m_downloadTimer = new QTimer(this);
    m_downloadTimer->setSingleShot(true);
    connect(m_downloadTimer, &QTimer::timeout, this, [this]() {
        qWarning() << "[UPDATE] download timed out";
        fail(tr("Could not download the update."));
    });

    m_disconnectTimer = new QTimer(this);
    m_disconnectTimer->setSingleShot(true);
    connect(m_disconnectTimer, &QTimer::timeout, this, &AppUpdateController::onDisconnectTimeout);

    if (m_connection) {
        connect(m_connection, &ConnectionController::connectionStateChanged, this,
                &AppUpdateController::onConnectionStateChanged);
    }

    if (isSupported()) {
        QTimer::singleShot(5000, this, [this]() {
            if (m_state == AppUpdate::Idle) {
                startCheck(false);
            }
        });
    }
}

AppUpdateController::~AppUpdateController()
{
    resetTransfer(false);
    if (m_checkReply) {
        m_checkReply->disconnect(this);
        m_checkReply->abort();
        m_checkReply->deleteLater();
        m_checkReply = nullptr;
    }
}

bool AppUpdateController::isSupported() const
{
    return !platformKey().isEmpty();
}

void AppUpdateController::checkForUpdates()
{
    startCheck(true);
}

void AppUpdateController::startCheck(bool interactive)
{
    if (!isSupported()) {
        return;
    }
    if (m_state == AppUpdate::Downloading || m_state == AppUpdate::Installing) {
        return;
    }
    // a manual check is already on the wire; don't replace it with the startup probe
    if (!interactive && m_checkReply) {
        return;
    }

    if (m_checkReply) {
        m_checkReply->disconnect(this);
        m_checkReply->abort();
        m_checkReply->deleteLater();
        m_checkReply = nullptr;
    }

    m_checkInteractive = interactive;
    if (interactive) {
        setState(AppUpdate::Checking);
    }

    QNetworkRequest request = makeRequest(kManifestUrl);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(20000);
    m_checkReply = amnApp->networkManager()->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, &AppUpdateController::onCheckFinished);
}

void AppUpdateController::onCheckFinished()
{
    if (!m_checkReply) {
        return;
    }

    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;

    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError err = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl finalUrl = reply->url();
    const bool interactive = m_checkInteractive;
    reply->deleteLater();

    const auto giveUp = [this, interactive](const QString &message) {
        qWarning().noquote() << "[UPDATE]" << message;
        if (!interactive) {
            if (m_state == AppUpdate::Checking) {
                setState(AppUpdate::Idle);
            }
            return;
        }
        fail(message);
    };

    if (err != QNetworkReply::NoError || status != 200 || !isTrustedHttps(finalUrl) || body.size() > kMaxManifestBytes) {
        giveUp(tr("Could not check for updates. Try again later."));
        return;
    }

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QString version = root.value(QStringLiteral("version")).toString().trimmed();
    static const QRegularExpression versionRe(QStringLiteral("^\\d+(?:\\.\\d+){1,3}$"));
    if (!versionRe.match(version).hasMatch()) {
        giveUp(tr("The update server returned an unexpected response."));
        return;
    }

    const QJsonObject platforms = root.value(QStringLiteral("platforms")).toObject();
    const QJsonObject package = platforms.value(platformKey()).toObject();
    const QUrl packageUrl(package.value(QStringLiteral("url")).toString());
    const QByteArray sha256 = parseSha256(package.value(QStringLiteral("sha256")).toString());
    const QString fileName = safeFileName(packageUrl);

    if (m_latestVersion != version) {
        m_latestVersion = version;
        emit latestVersionChanged();
    }

    if (compareVersions(QStringLiteral(APP_VERSION), version) >= 0) {
        m_packageUrl.clear();
        m_expectedSha256.clear();
        if (!m_releaseNotes.isEmpty()) {
            m_releaseNotes.clear();
            emit releaseNotesChanged();
        }
        qInfo() << "[UPDATE] up to date" << APP_VERSION;
        setState(AppUpdate::UpToDate);
        return;
    }

    if (!isTrustedHttps(packageUrl) || sha256.size() != 32 || !acceptedPackageName(fileName)) {
        giveUp(tr("No installer is published for this system."));
        return;
    }

    m_packageUrl = packageUrl.toString();
    m_expectedSha256 = sha256;
    const QString notes = notesFromManifest(root);
    if (m_releaseNotes != notes) {
        m_releaseNotes = notes;
        emit releaseNotesChanged();
    }
    qInfo() << "[UPDATE] available" << version;
    setState(AppUpdate::UpdateAvailable);
}

QString AppUpdateController::notesFromManifest(const QJsonObject &root) const
{
    QString key = QStringLiteral("notes");
    if (m_language) {
        switch (static_cast<LanguageSettings::AvailableLanguageEnum>(m_language->getCurrentLanguageIndex())) {
        case LanguageSettings::AvailableLanguageEnum::Russian: key = QStringLiteral("notes_ru"); break;
        case LanguageSettings::AvailableLanguageEnum::Ukrainian: key = QStringLiteral("notes_uk"); break;
        default: break;
        }
    }
    QString notes = root.value(key).toString().trimmed();
    if (notes.isEmpty()) {
        notes = root.value(QStringLiteral("notes")).toString().trimmed();
    }
    if (notes.size() > 2000) {
        notes.truncate(2000);
    }
    return notes;
}

void AppUpdateController::downloadAndInstall()
{
    if (!isSupported() || m_state != AppUpdate::UpdateAvailable) {
        return;
    }
    if (m_packageUrl.isEmpty() || m_expectedSha256.size() != 32) {
        fail(tr("No installer is published for this system."));
        return;
    }

    const QUrl packageUrl(m_packageUrl);
    const QString fileName = safeFileName(packageUrl);
    if (!isTrustedHttps(packageUrl) || !acceptedPackageName(fileName)) {
        fail(tr("No installer is published for this system."));
        return;
    }

    resetTransfer(true);

    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/DopamineUpdate");
    QDir().mkpath(dirPath);
    m_packagePath = dirPath + QLatin1Char('/') + fileName;

    m_file = new QFile(m_packagePath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        qWarning() << "[UPDATE] cannot write" << m_packagePath;
        fail(tr("Could not download the update."));
        return;
    }
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);

    setProgress(-1);
    setState(AppUpdate::Downloading);

    QNetworkRequest request = makeRequest(packageUrl);
    request.setRawHeader("Accept", "*/*");
    request.setTransferTimeout(0);
    m_reply = amnApp->networkManager()->get(request);
    m_stallTimer->start(kStallTimeoutMs);
    m_downloadTimer->start(kDownloadTimeoutMs);

    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (received > kMaxPackageBytes || (total > 0 && total > kMaxPackageBytes)) {
            qWarning() << "[UPDATE] package is too large";
            fail(tr("Could not download the update."));
            return;
        }
        if (total > 0) {
            setProgress(double(received) / double(total));
        }
        m_stallTimer->start(kStallTimeoutMs);
    });

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_reply || !m_file || !m_hash) {
            return;
        }
        const QByteArray chunk = m_reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        if (m_file->write(chunk) != chunk.size()) {
            qWarning() << "[UPDATE] write failed";
            fail(tr("Could not download the update."));
            return;
        }
        m_hash->addData(chunk);
    });

    connect(m_reply, &QNetworkReply::finished, this, &AppUpdateController::onDownloadFinished);
}

void AppUpdateController::onDownloadFinished()
{
    if (!m_reply) {
        return;
    }

    QNetworkReply *reply = m_reply;
    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl finalUrl = reply->url();

    if (m_file && m_hash && reply->bytesAvailable() > 0) {
        const QByteArray chunk = reply->readAll();
        if (m_file->write(chunk) != chunk.size()) {
            reply->disconnect(this);
            reply->deleteLater();
            m_reply = nullptr;
            fail(tr("Could not download the update."));
            return;
        }
        m_hash->addData(chunk);
    }

    QByteArray digest;
    if (m_hash) {
        digest = m_hash->result();
    }
    if (m_file) {
        m_file->flush();
        m_file->close();
    }

    reply->disconnect(this);
    reply->deleteLater();
    m_reply = nullptr;
    m_stallTimer->stop();
    m_downloadTimer->stop();
    delete m_file;
    m_file = nullptr;
    delete m_hash;
    m_hash = nullptr;

    if (err != QNetworkReply::NoError || status != 200 || !isTrustedHttps(finalUrl)) {
        qWarning() << "[UPDATE] download failed:" << errString << "status:" << status;
        QFile::remove(m_packagePath);
        fail(tr("Could not download the update."));
        return;
    }
    if (digest != m_expectedSha256) {
        qWarning() << "[UPDATE] sha256 mismatch";
        QFile::remove(m_packagePath);
        fail(tr("The update file failed the integrity check."));
        return;
    }
    if (!isTrustedPackage(m_packagePath)) {
        qWarning() << "[UPDATE] untrusted signature";
        fail(tr("The update file has an untrusted signature."));
        return;
    }

    qInfo() << "[UPDATE] package verified" << m_packagePath;
    disconnectThenInstall();
}

void AppUpdateController::disconnectThenInstall()
{
    setState(AppUpdate::Installing);
    if (!m_connection || (!m_connection->isConnected() && !m_connection->isConnectionInProgress())) {
        QTimer::singleShot(200, this, &AppUpdateController::launchAndQuit);
        return;
    }

    qInfo() << "[UPDATE] disconnecting before install";
    m_waitingForDisconnect = true;
    m_disconnectTimer->start(kDisconnectTimeoutMs);
    m_connection->closeConnection();
}

void AppUpdateController::onConnectionStateChanged()
{
    if (!m_waitingForDisconnect || !m_connection) {
        return;
    }
    if (m_connection->isConnected() || m_connection->isConnectionInProgress()) {
        return;
    }
    m_waitingForDisconnect = false;
    m_disconnectTimer->stop();
    // the service needs a moment to drop wireguard-go / wintun before files are replaced
    QTimer::singleShot(1500, this, &AppUpdateController::launchAndQuit);
}

void AppUpdateController::onDisconnectTimeout()
{
    if (!m_waitingForDisconnect) {
        return;
    }
    m_waitingForDisconnect = false;
    qWarning() << "[UPDATE] disconnect timed out";
    fail(tr("Disconnect the VPN, then try the update again."));
}

void AppUpdateController::launchAndQuit()
{
    if (m_launchStarted || m_state != AppUpdate::Installing) {
        return;
    }
    m_launchStarted = true;

    if (!startInstaller(m_packagePath)) {
        m_launchStarted = false;
        fail(tr("Could not start the installer."));
        return;
    }

    qInfo() << "[UPDATE] installer started, quitting";
    QTimer::singleShot(400, QCoreApplication::instance(), &QCoreApplication::quit);
}

bool AppUpdateController::startInstaller(const QString &path) const
{
    if (!QFileInfo::exists(path)) {
        qWarning() << "[UPDATE] package missing" << path;
        return false;
    }

#if defined(Q_OS_WIN)
    const QString msiexec = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"))
            + QStringLiteral("\\System32\\msiexec.exe");
    const QString installed = qEnvironmentVariable("ProgramFiles", QStringLiteral("C:\\Program Files"))
            + QStringLiteral("\\Dopamine\\Dopamine.exe");
    const QString scriptPath = QFileInfo(path).dir().filePath(QStringLiteral("install-update.cmd"));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[UPDATE] cannot write installer script";
        return QProcess::startDetached(msiexec, { QStringLiteral("/i"), QDir::toNativeSeparators(path) });
    }
    QTextStream out(&script);
    out << "\"" << QDir::toNativeSeparators(msiexec) << "\" /i \"" << QDir::toNativeSeparators(path) << "\"\r\n";
    out << "if %ERRORLEVEL% EQU 0 start \"\" \"" << QDir::toNativeSeparators(installed) << "\"\r\n";
    script.close();
    // cmd /c waits for msiexec, then starts the new build. The path is quoted in the script.
    return QProcess::startDetached(QStringLiteral("cmd.exe"), { QStringLiteral("/c"), QDir::toNativeSeparators(scriptPath) });
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), { path });
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return QProcess::startDetached(path, {});
#else
    Q_UNUSED(path);
    return false;
#endif
}

void AppUpdateController::fail(const QString &message)
{
    resetTransfer(true);
    m_waitingForDisconnect = false;
    if (m_disconnectTimer) {
        m_disconnectTimer->stop();
    }
    if (m_errorMessage != message) {
        m_errorMessage = message;
        emit errorMessageChanged();
    }
    m_launchStarted = false;
    setState(AppUpdate::Error);
}

void AppUpdateController::resetTransfer(bool removePackage)
{
    if (m_stallTimer) {
        m_stallTimer->stop();
    }
    if (m_downloadTimer) {
        m_downloadTimer->stop();
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    delete m_file;
    m_file = nullptr;
    delete m_hash;
    m_hash = nullptr;
    if (removePackage && !m_packagePath.isEmpty()) {
        QFile::remove(m_packagePath);
    }
}

void AppUpdateController::setState(AppUpdate::State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    if (state != AppUpdate::Error && !m_errorMessage.isEmpty()) {
        m_errorMessage.clear();
        emit errorMessageChanged();
    }
    emit stateChanged();
}

void AppUpdateController::setProgress(double progress)
{
    if (qAbs(m_progress - progress) < 0.001) {
        return;
    }
    m_progress = progress;
    emit progressChanged();
}
