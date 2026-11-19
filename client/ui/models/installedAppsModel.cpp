#include "installedAppsModel.h"

#include <QEventLoop>
#include <QtConcurrent>

#ifdef Q_OS_ANDROID
    #include "platforms/android/android_controller.h"
#endif

InstalledAppsModel::InstalledAppsModel(QObject *parent) : QAbstractListModel(parent)
{
}

int InstalledAppsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_installedApps.size();
}

QVariant InstalledAppsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rowCount()))
        return QVariant();

    switch (role) {
    case AppNameRole: {
        auto appName = m_installedApps.at(index.row()).toObject().value("name").toString();
        auto packageName = m_installedApps.at(index.row()).toObject().value("package").toString();
        if (appName.isEmpty()) {
            appName = packageName;
        }
        return appName;
    }
    case AppIconRole: {
        return m_installedApps.at(index.row()).toObject().value("package").toString();
    }
    case PackageNameRole: {
        return m_installedApps.at(index.row()).toObject().value("package");
    }
    case IsAppSelectedRole: {
        return m_selectedAppIndexes.contains(index.row());
    }
    }

    return QVariant();
}

void InstalledAppsModel::selectedStateChanged(const int index, const bool selected)
{
    if (selected) {
        m_selectedAppIndexes.insert(index);
    } else {
        m_selectedAppIndexes.remove(index);
    }
}

QVector<QPair<QString, QString>> InstalledAppsModel::getSelectedAppsInfo()
{
    QVector<QPair<QString, QString>> appsInfo;
    for (const auto i : m_selectedAppIndexes) {
        QString packageName = data(index(i, 0), PackageNameRole).toString();
        QString appName = data(index(i, 0), AppNameRole).toString();
        if (appName.isEmpty()) {
            appName = packageName;
        }

        appsInfo.push_back({ appName, packageName });
    }

    m_selectedAppIndexes.clear();
    return appsInfo;
}

void InstalledAppsModel::updateModel()
{
    // Only the slow JNI call runs on the pool thread; the model reset must happen
    // on the thread that owns the model.
    QFuture<QJsonArray> future = QtConcurrent::run([]() -> QJsonArray {
#ifdef Q_OS_ANDROID
        return AndroidController::instance()->getAppList();
#else
        return QJsonArray();
#endif
    });

    QFutureWatcher<QJsonArray> watcher;
    QEventLoop wait;
    connect(&watcher, &QFutureWatcher<QJsonArray>::finished, &wait, &QEventLoop::quit);
    watcher.setFuture(future);
    wait.exec();

    beginResetModel();
    m_installedApps = future.result();
    m_selectedAppIndexes.clear();
    endResetModel();

    return;
}

QHash<int, QByteArray> InstalledAppsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AppNameRole] = "appName";
    roles[AppIconRole] = "appIcon";
    roles[PackageNameRole] = "packageName";
    roles[IsAppSelectedRole] = "isAppSelected";
    return roles;
}
