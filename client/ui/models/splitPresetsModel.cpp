#include "splitPresetsModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/api/apiDefs.h"
#include "core/controllers/gatewayController.h"

SplitPresetsModel::SplitPresetsModel(std::shared_ptr<Settings> settings, const QSharedPointer<ServersModel> &serversModel,
                                     QObject *parent)
    : QAbstractListModel(parent), m_settings(settings), m_serversModel(serversModel)
{
    const QStringList enabled = m_settings->splitPresetsEnabled();
    m_enabledPresets = QSet<QString>(enabled.begin(), enabled.end());
    loadFromCache();
}

int SplitPresetsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_presets.size();
}

QVariant SplitPresetsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_presets.size())) {
        return QVariant();
    }

    const Preset &preset = m_presets.at(index.row());
    switch (role) {
    case PresetIdRole: return preset.id;
    case NameRole: return preset.name;
    case DomainsCountRole: return preset.domains.size();
    case EnabledRole: return m_enabledPresets.contains(preset.id);
    default: return QVariant();
    }
}

QHash<int, QByteArray> SplitPresetsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[PresetIdRole] = "presetId";
    roles[NameRole] = "name";
    roles[DomainsCountRole] = "domainsCount";
    roles[EnabledRole] = "enabled";
    return roles;
}

int SplitPresetsModel::routeMode() const
{
    return m_settings->splitPresetsRouteMode();
}

void SplitPresetsModel::setRouteMode(int routeMode)
{
    if (m_settings->splitPresetsRouteMode() == routeMode) {
        return;
    }
    m_settings->setSplitPresetsRouteMode(routeMode);
    emit routeModeChanged();
}

void SplitPresetsModel::fetchPresets()
{
    if (m_serversModel.isNull()) {
        return;
    }
    const auto stacks = m_serversModel->gatewayStacks();
    if (stacks.isEmpty()) {
        return;
    }

    auto gatewayController = QSharedPointer<GatewayController>::create(m_settings->getGatewayEndpoint(), m_settings->isDevGatewayEnv(),
                                                                       apiDefs::requestTimeoutMsecs, m_settings->isStrictKillSwitchEnabled());
    QJsonObject payload;
    payload.insert("locale", m_settings->getAppLanguage().name().split("_").first());
    if (!m_version.isEmpty()) {
        payload.insert("presets_version", m_version);
    }

    auto future = gatewayController->postAsync(QString("%1v1/split_presets"), payload);
    future.then(this, [this, gatewayController](QPair<ErrorCode, QByteArray> result) {
        const auto [errorCode, responseBody] = result;
        if (errorCode != ErrorCode::NoError) {
            // silent: cached presets stay in effect
            emit fetchPresetsFinished();
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(responseBody).object();
        const QString newVersion = obj.value("version").toString();
        const QJsonArray presetsArray = obj.value("presets").toArray();

        // empty list with the same version = cache is still valid
        if (!newVersion.isEmpty() && newVersion == m_version && presetsArray.isEmpty()) {
            emit fetchPresetsFinished();
            return;
        }

        beginResetModel();
        m_presets.clear();
        for (const auto &value : presetsArray) {
            const QJsonObject presetObj = value.toObject();
            Preset preset;
            preset.id = presetObj.value("id").toString();
            preset.name = presetObj.value("name").toString();
            const QJsonArray domains = presetObj.value("domains").toArray();
            for (const auto &domain : domains) {
                preset.domains.append(domain.toString());
            }
            if (!preset.id.isEmpty() && !preset.domains.isEmpty()) {
                m_presets.append(preset);
            }
        }
        m_version = newVersion;
        endResetModel();

        saveToCache();
        emit countChanged();
        emit fetchPresetsFinished();
    });
}

void SplitPresetsModel::setPresetEnabled(int row, bool enabled)
{
    if (row < 0 || row >= m_presets.size()) {
        return;
    }

    const QString id = m_presets.at(row).id;
    const bool wasEnabled = m_enabledPresets.contains(id);
    if (wasEnabled == enabled) {
        return;
    }

    if (enabled) {
        m_enabledPresets.insert(id);
    } else {
        m_enabledPresets.remove(id);
    }
    m_settings->setSplitPresetsEnabled(m_enabledPresets.values());

    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, { EnabledRole });
    emit enabledCountChanged();
}

void SplitPresetsModel::loadFromCache()
{
    m_version = m_settings->splitPresetsVersion();
    const QByteArray cache = m_settings->splitPresetsCache().toUtf8();
    if (cache.isEmpty()) {
        return;
    }

    const QJsonArray presetsArray = QJsonDocument::fromJson(cache).array();
    for (const auto &value : presetsArray) {
        const QJsonObject presetObj = value.toObject();
        Preset preset;
        preset.id = presetObj.value("id").toString();
        preset.name = presetObj.value("name").toString();
        const QJsonArray domains = presetObj.value("domains").toArray();
        for (const auto &domain : domains) {
            preset.domains.append(domain.toString());
        }
        if (!preset.id.isEmpty() && !preset.domains.isEmpty()) {
            m_presets.append(preset);
        }
    }
}

void SplitPresetsModel::saveToCache() const
{
    QJsonArray presetsArray;
    for (const auto &preset : m_presets) {
        QJsonObject presetObj;
        presetObj.insert("id", preset.id);
        presetObj.insert("name", preset.name);
        presetObj.insert("domains", QJsonArray::fromStringList(preset.domains));
        presetsArray.append(presetObj);
    }
    m_settings->setSplitPresetsCache(QString::fromUtf8(QJsonDocument(presetsArray).toJson(QJsonDocument::Compact)));
    m_settings->setSplitPresetsVersion(m_version);
}
