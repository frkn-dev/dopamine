#include "splitPresetsModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/api/apiDefs.h"
#include "core/builtinSplitPresets.h"
#include "core/controllers/gatewayController.h"

SplitPresetsModel::SplitPresetsModel(std::shared_ptr<Settings> settings, const QSharedPointer<ServersModel> &serversModel,
                                     QObject *parent)
    : QAbstractListModel(parent), m_settings(settings), m_serversModel(serversModel)
{
    const QStringList enabled = m_settings->splitPresetsEnabled();
    m_enabledPresets = QSet<QString>(enabled.begin(), enabled.end());
    loadFromCache();
    appendBuiltinPresets();
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
    case DescriptionRole: return preset.description;
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
    roles[DescriptionRole] = "description";
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
    // the presets catalog is public (no auth, like news) — no gateway stacks
    // required: users with manually added configs must get presets too
    qDebug() << "[PRESETS] fetching, cached version:" << m_version;

    auto gatewayController = QSharedPointer<GatewayController>::create(m_settings->getGatewayEndpoint(), m_settings->isDevGatewayEnv(),
                                                                       apiDefs::requestTimeoutMsecs, m_settings->isStrictKillSwitchEnabled(),
                                                                       nullptr, m_settings->getGatewayEndpointFallback());
    QJsonObject payload;
    payload.insert("locale", m_settings->getAppLanguage().name().split("_").first());
    // send the cached version only when we actually hold the preset list:
    // the server treats a matching version as "client cache is fresh" and
    // returns an empty catalog — with an empty local list that would leave
    // the user with no presets forever
    if (!m_version.isEmpty() && !m_presets.isEmpty()) {
        payload.insert("presets_version", m_version);
    }

    auto future = gatewayController->postAsync(QString("%1v1/split_presets"), payload);
    future.then(this, [this, gatewayController](QPair<ErrorCode, QByteArray> result) {
        const auto [errorCode, responseBody] = result;
        if (errorCode != ErrorCode::NoError) {
            qWarning() << "[PRESETS] fetch failed:" << static_cast<int>(errorCode);
            // silent: cached presets stay in effect
            emit fetchPresetsFinished();
            return;
        }

        const QJsonObject obj = QJsonDocument::fromJson(responseBody).object();
        const QString newVersion = obj.value("version").toString();
        const QJsonArray presetsArray = obj.value("presets").toArray();
        qDebug() << "[PRESETS] response: version" << newVersion << "count" << presetsArray.size();

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
            preset.description = presetObj.value("description").toString();
            const QJsonArray domains = presetObj.value("domains").toArray();
            for (const auto &domain : domains) {
                preset.domains.append(domain.toString());
            }
            if (!preset.id.isEmpty() && !preset.domains.isEmpty()) {
                m_presets.append(preset);
            }
        }
        appendBuiltinPresets();
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
        // a stored version without the preset list is useless — refetch fresh
        m_version.clear();
        return;
    }

    const QJsonArray presetsArray = QJsonDocument::fromJson(cache).array();
    for (const auto &value : presetsArray) {
        const QJsonObject presetObj = value.toObject();
        Preset preset;
        preset.id = presetObj.value("id").toString();
        preset.name = presetObj.value("name").toString();
        preset.description = presetObj.value("description").toString();
        const QJsonArray domains = presetObj.value("domains").toArray();
        for (const auto &domain : domains) {
            preset.domains.append(domain.toString());
        }
        if (!preset.id.isEmpty() && !preset.domains.isEmpty()) {
            m_presets.append(preset);
        }
    }
}

void SplitPresetsModel::appendBuiltinPresets()
{
    // builtin presets go FIRST in the list: they are our own RU routing
    // shortcuts and should be visible above the API catalog
    QList<Preset> builtin;
    for (const auto &value : BuiltinSplitPresets::presets()) {
        const QJsonObject presetObj = value.toObject();
        Preset preset;
        preset.id = presetObj.value("id").toString();
        // an API preset with the same id wins
        bool duplicate = false;
        for (const auto &existing : m_presets) {
            if (existing.id == preset.id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        preset.name = presetObj.value("name").toString();
        preset.description = presetObj.value("description").toString();
        const QJsonArray domains = presetObj.value("domains").toArray();
        for (const auto &domain : domains) {
            preset.domains.append(domain.toString());
        }
        if (!preset.id.isEmpty() && !preset.domains.isEmpty()) {
            m_builtinIds.insert(preset.id);
            builtin.append(preset);
        }
    }
    for (int i = builtin.size() - 1; i >= 0; --i) {
        m_presets.prepend(builtin.at(i));
    }
}

void SplitPresetsModel::saveToCache() const
{
    QJsonArray presetsArray;
    for (const auto &preset : m_presets) {
        // the cache mirrors the API catalog (and its version) — builtin presets
        // are merged in code and must not be persisted there
        if (m_builtinIds.contains(preset.id)) {
            continue;
        }
        QJsonObject presetObj;
        presetObj.insert("id", preset.id);
        presetObj.insert("name", preset.name);
        if (!preset.description.isEmpty()) {
            presetObj.insert("description", preset.description);
        }
        presetObj.insert("domains", QJsonArray::fromStringList(preset.domains));
        presetsArray.append(presetObj);
    }
    m_settings->setSplitPresetsCache(QString::fromUtf8(QJsonDocument(presetsArray).toJson(QJsonDocument::Compact)));
    m_settings->setSplitPresetsVersion(m_version);
}
