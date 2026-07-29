#include "splitPresetsModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/api/apiDefs.h"
#include "core/controllers/gatewayController.h"

SplitPresetsModel::SplitPresetsModel(std::shared_ptr<Settings> settings, const QSharedPointer<SitesModel> &sitesModel,
                                     const QSharedPointer<ServersModel> &serversModel, QObject *parent)
    : QAbstractListModel(parent), m_settings(settings), m_sitesModel(sitesModel), m_serversModel(serversModel)
{
    m_enabledPresets = QSet<QString>(m_settings->splitPresetsEnabled().begin(), m_settings->splitPresetsEnabled().end());
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

        const QList<Preset> oldPresets = m_presets;

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
        reapplyEnabledPresets(oldPresets);
        emit countChanged();
        emit fetchPresetsFinished();
    });
}

void SplitPresetsModel::setPresetEnabled(int row, bool enabled)
{
    if (row < 0 || row >= m_presets.size()) {
        return;
    }

    const Preset &preset = m_presets.at(row);
    const bool wasEnabled = m_enabledPresets.contains(preset.id);
    if (wasEnabled == enabled) {
        return;
    }

    if (enabled) {
        m_enabledPresets.insert(preset.id);
    } else {
        m_enabledPresets.remove(preset.id);
    }
    m_settings->setSplitPresetsEnabled(m_enabledPresets.values());

    applyPreset(preset, enabled);

    const QModelIndex modelIndex = index(row);
    emit dataChanged(modelIndex, modelIndex, { EnabledRole });
}

void SplitPresetsModel::applyPreset(const Preset &preset, bool enabled)
{
    if (m_sitesModel.isNull()) {
        return;
    }
    if (enabled) {
        QMap<QString, QString> sites;
        for (const auto &domain : preset.domains) {
            sites.insert(domain, QString());
        }
        m_sitesModel->addSites(sites, false);
    } else {
        m_sitesModel->removeSitesByDomains(preset.domains);
    }
}

void SplitPresetsModel::reapplyEnabledPresets(const QList<Preset> &oldPresets)
{
    // backend may have changed a bundle: swap old domains for new ones on enabled presets
    for (const auto &newPreset : m_presets) {
        if (!m_enabledPresets.contains(newPreset.id)) {
            continue;
        }
        int oldIndex = -1;
        for (int i = 0; i < oldPresets.size(); ++i) {
            if (oldPresets.at(i).id == newPreset.id) {
                oldIndex = i;
                break;
            }
        }
        if (oldIndex >= 0 && oldPresets.at(oldIndex).domains != newPreset.domains) {
            applyPreset(oldPresets.at(oldIndex), false);
            applyPreset(newPreset, true);
        }
    }
}

int SplitPresetsModel::indexOfPreset(const QString &id) const
{
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets.at(i).id == id) {
            return i;
        }
    }
    return -1;
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
