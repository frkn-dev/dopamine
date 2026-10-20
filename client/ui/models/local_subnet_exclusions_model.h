#ifndef LOCALSUBNETEXCLUSIONSMODEL_H
#define LOCALSUBNETEXCLUSIONSMODEL_H

#include <QAbstractListModel>
#include "settings.h"

/**
 * Local subnets that must bypass the tunnel (Settings -> Local subnets).
 *
 * The list is persisted in `Conf/localSubnetExclusions` through Settings,
 * which normalizes it: IPv4/IPv6 CIDR, bare addresses widened to /32 or /128,
 * no duplicates, no `/0`.
 */
class LocalSubnetExclusionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        SubnetRole = Qt::UserRole + 1
    };

    explicit LocalSubnetExclusionsModel(std::shared_ptr<Settings> settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

public slots:
    // Returns false when the (normalized) subnet is already in the list.
    bool addSubnet(const QString &subnet);
    // Returns how many subnets were actually added.
    int addSubnets(const QStringList &subnets, bool replaceExisting);
    void removeSubnet(QModelIndex index);
    void removeSubnets();
    QStringList getSubnets() const;

protected:
    QHash<int, QByteArray> roleNames() const override;

private:
    void fillSubnets();
    void persist();

    std::shared_ptr<Settings> m_settings;
    QStringList m_subnets;
};

#endif // LOCALSUBNETEXCLUSIONSMODEL_H
