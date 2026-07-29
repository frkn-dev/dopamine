#ifndef SPLITPRESETSMODEL_H
#define SPLITPRESETSMODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QSharedPointer>

#include "settings.h"
#include "sites_model.h"
#include "servers_model.h"

// Split-tunneling service presets (see frkn-docs/api-split-presets.md):
// bundles of domains per service (YouTube, ChatGPT, ...) fetched from the
// gateway and applied to the split-tunneling site list as a group.
class SplitPresetsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        PresetIdRole = Qt::UserRole + 1,
        NameRole,
        DomainsCountRole,
        EnabledRole
    };

    explicit SplitPresetsModel(std::shared_ptr<Settings> settings, const QSharedPointer<SitesModel> &sitesModel,
                               const QSharedPointer<ServersModel> &serversModel, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

    Q_INVOKABLE void fetchPresets();
    Q_INVOKABLE void setPresetEnabled(int row, bool enabled);

signals:
    void countChanged();
    void fetchPresetsFinished();

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Preset
    {
        QString id;
        QString name;
        QStringList domains;
    };

    void loadFromCache();
    void saveToCache() const;
    void applyPreset(const Preset &preset, bool enabled);
    void reapplyEnabledPresets(const QList<Preset> &oldPresets);
    int indexOfPreset(const QString &id) const;

    std::shared_ptr<Settings> m_settings;
    QSharedPointer<SitesModel> m_sitesModel;
    QSharedPointer<ServersModel> m_serversModel;

    QList<Preset> m_presets;
    QSet<QString> m_enabledPresets;
    QString m_version;
};

#endif // SPLITPRESETSMODEL_H
