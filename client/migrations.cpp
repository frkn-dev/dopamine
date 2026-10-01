#include "migrations.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "version.h"

Migrations::Migrations(QObject *parent)
    : QObject{parent}
{
    QString version(APP_MAJOR_VERSION);

    QStringList versionDigits = version.split(".");

    if (versionDigits.size() >= 3) {
        currentMajor = versionDigits[0].toInt();
        currentMinor = versionDigits[1].toInt();
        currentMicro = versionDigits[2].toInt();
    }

    if (versionDigits.size() == 4) {
        currentPatch = versionDigits[3].toInt();
    }
}

void Migrations::doMigrations()
{
    if (currentMajor == 3) {
        migrateV3();
    }
    migrateAndroidAppSettings();
}

void Migrations::migrateV3()
{
#ifdef Q_OS_ANDROID
    qDebug() << "Migration to V3 on Android...";

    QString packageName = "org.frkn.dopamine";

    QDir dir(".");
    QString currentDir = dir.absolutePath();

    int packageNameIndex = currentDir.indexOf(packageName);

    if (packageNameIndex == -1) {
        return;
    }

    QString rootLocation = currentDir.left(packageNameIndex + packageName.size());

    if (rootLocation.isEmpty()) {
        return;
    }

    QString location = rootLocation + "/files/.config/FRKN.ORG/FRKN.conf";

    QFile oldConfig(location);

    if (oldConfig.exists()) {
        QString newConfigPath = rootLocation + "/files/settings";

        QDir newConfigDir(newConfigPath);

        newConfigPath += "/FRKN.ORG";

        bool mkPathRes = newConfigDir.mkpath(newConfigPath);

        if (!mkPathRes) {
            return;
        }

        QFile newConfigFile(newConfigPath + "/FRKN.conf");

        if (!newConfigFile.exists()) {
            bool cpResult = QFile::copy(oldConfig.fileName(), newConfigFile.fileName());
            if (cpResult) {
                oldConfig.remove();
                QDir oldConfigDir(rootLocation + "/files/.config");
                oldConfigDir.rmdir("FRKN.ORG");
            }
        }
    }
#endif
}

#ifdef Q_OS_ANDROID
namespace {
bool confHasServers(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.readAll().contains("serversList");
}

QString androidDataRoot()
{
    const QString packageName = QStringLiteral("org.frkn.dopamine");
    const QString currentDir = QDir(QStringLiteral(".")).absolutePath();
    const int packageNameIndex = currentDir.indexOf(packageName);
    if (packageNameIndex < 0) {
        return {};
    }
    return currentDir.left(packageNameIndex + packageName.size());
}
}
#endif

void Migrations::migrateAndroidAppSettings()
{
#ifdef Q_OS_ANDROID
    const QString root = androidDataRoot();
    if (root.isEmpty()) {
        return;
    }

    const QString settingsDir = root + QStringLiteral("/files/settings/") + QLatin1String(ORGANIZATION_NAME);
    const QString currentFile = settingsDir + QLatin1Char('/') + QLatin1String(APPLICATION_NAME) + QStringLiteral(".conf");
    if (confHasServers(currentFile)) {
        return;
    }

    const QStringList legacyFiles = {
        settingsDir + QStringLiteral("/FRKN.conf"),
        root + QStringLiteral("/files/.config/FRKN.ORG/FRKN.conf"),
        root + QStringLiteral("/files/settings/AmneziaVPN.ORG/AmneziaVPN.conf"),
        root + QStringLiteral("/files/.config/AmneziaVPN.ORG/AmneziaVPN.conf"),
    };

    for (const QString &legacyFile : legacyFiles) {
        if (legacyFile == currentFile || !confHasServers(legacyFile)) {
            continue;
        }
        if (!QDir().mkpath(settingsDir)) {
            return;
        }
        if (QFile::exists(currentFile)) {
            QFile::remove(currentFile);
        }
        if (QFile::copy(legacyFile, currentFile)) {
            qDebug() << "Migrated Android settings from" << legacyFile;
        }
        return;
    }
#endif
}
