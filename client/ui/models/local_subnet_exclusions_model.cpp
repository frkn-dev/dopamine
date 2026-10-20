#include "local_subnet_exclusions_model.h"

#include "core/networkUtilities.h"

LocalSubnetExclusionsModel::LocalSubnetExclusionsModel(std::shared_ptr<Settings> settings, QObject *parent)
    : QAbstractListModel(parent), m_settings(settings)
{
    fillSubnets();
}

int LocalSubnetExclusionsModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_subnets.size();
}

QVariant LocalSubnetExclusionsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rowCount()))
        return QVariant();

    switch (role) {
    case SubnetRole:
        return m_subnets.at(index.row());
    default:
        return QVariant();
    }
}

bool LocalSubnetExclusionsModel::addSubnet(const QString &subnet)
{
    // Normalize here as well: any caller (panel, import, automation) may hand
    // over a raw entry.
    const QStringList normalized = NetworkUtilities::normalizeSubnetExclusions(QStringList { subnet });
    if (normalized.isEmpty()) {
        return false;
    }

    const QString value = normalized.first();
    if (m_subnets.contains(value)) {
        return false;
    }

    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_subnets.append(value);
    persist();
    endInsertRows();
    return true;
}

int LocalSubnetExclusionsModel::addSubnets(const QStringList &subnets, bool replaceExisting)
{
    const QStringList normalized = NetworkUtilities::normalizeSubnetExclusions(subnets);

    beginResetModel();

    if (replaceExisting) {
        m_subnets.clear();
    }

    int added = 0;
    for (const QString &subnet : normalized) {
        if (!m_subnets.contains(subnet)) {
            m_subnets.append(subnet);
            added++;
        }
    }

    persist();
    endResetModel();
    return added;
}

void LocalSubnetExclusionsModel::removeSubnet(QModelIndex index)
{
    if (!index.isValid() || index.row() >= m_subnets.size()) {
        return;
    }

    beginRemoveRows(QModelIndex(), index.row(), index.row());
    m_subnets.removeAt(index.row());
    persist();
    endRemoveRows();
}

void LocalSubnetExclusionsModel::removeSubnets()
{
    if (m_subnets.isEmpty()) {
        return;
    }

    beginResetModel();
    m_subnets.clear();
    persist();
    endResetModel();
}

QStringList LocalSubnetExclusionsModel::getSubnets() const
{
    return m_subnets;
}

QHash<int, QByteArray> LocalSubnetExclusionsModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[SubnetRole] = "subnet";
    return roles;
}

void LocalSubnetExclusionsModel::fillSubnets()
{
    m_subnets = m_settings->localSubnetExclusions();
}

void LocalSubnetExclusionsModel::persist()
{
    m_settings->setLocalSubnetExclusions(m_subnets);
}
