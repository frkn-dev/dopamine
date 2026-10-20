#include "localSubnetExclusionsController.h"

#include <QRegularExpression>

#include "core/networkUtilities.h"

LocalSubnetExclusionsController::LocalSubnetExclusionsController(const std::shared_ptr<Settings> &settings,
                                                                 const QSharedPointer<LocalSubnetExclusionsModel> &model,
                                                                 QObject *parent)
    : QObject(parent), m_settings(settings), m_model(model)
{
}

void LocalSubnetExclusionsController::addSubnet(QString subnet)
{
    // A paste of several entries (separated by spaces, commas, semicolons or
    // newlines) is accepted as well as a single subnet.
    const QStringList entries =
        subnet.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts);
    if (entries.isEmpty()) {
        return;
    }

    QStringList rejected;
    const QStringList normalized = NetworkUtilities::normalizeSubnetExclusions(entries, &rejected);

    int added = 0;
    int duplicates = 0;
    for (const QString &value : normalized) {
        if (m_model->addSubnet(value)) {
            added++;
        } else {
            duplicates++;
        }
    }

    if (!rejected.isEmpty()) {
        emit errorOccurred(rejected.size() == 1
                               ? tr("%1 is not an IPv4/IPv6 subnet, use e.g. 192.168.0.0/16 or 10.0.0.1")
                                     .arg(rejected.first())
                               : tr("Not valid IPv4/IPv6 subnets: %1").arg(rejected.join(QStringLiteral(", "))));
    }
    if (duplicates > 0) {
        emit errorOccurred(tr("Local subnets already in the list: %1").arg(QString::number(duplicates)));
    }
    if (added > 0) {
        emit finished(tr("Local subnets added: %1").arg(QString::number(added)));
    }
}

void LocalSubnetExclusionsController::removeSubnet(int index)
{
    const QModelIndex modelIndex = m_model->index(index);
    const QString subnet = m_model->data(modelIndex, LocalSubnetExclusionsModel::SubnetRole).toString();
    m_model->removeSubnet(modelIndex);

    if (!subnet.isEmpty()) {
        emit finished(tr("Local subnet removed: %1").arg(subnet));
    }
}

void LocalSubnetExclusionsController::removeSubnets()
{
    m_model->removeSubnets();
    emit finished(tr("The list of local subnets has been cleared"));
}
