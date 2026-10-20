#ifndef LOCALSUBNETEXCLUSIONSCONTROLLER_H
#define LOCALSUBNETEXCLUSIONSCONTROLLER_H

#include <QObject>

#include "settings.h"
#include "ui/models/local_subnet_exclusions_model.h"

/**
 * Adds/removes local subnet exclusions (Settings -> Local subnets) and
 * translates validation failures into user-visible messages.
 *
 * The validation itself lives in NetworkUtilities/Settings: an entry must be a
 * valid IPv4/IPv6 address or CIDR subnet; a bare address is widened to /32
 * (IPv4) or /128 (IPv6), duplicates are dropped and `/0` is rejected because
 * excluding "everything" would disable the VPN.
 */
class LocalSubnetExclusionsController : public QObject
{
    Q_OBJECT
public:
    explicit LocalSubnetExclusionsController(const std::shared_ptr<Settings> &settings,
                                             const QSharedPointer<LocalSubnetExclusionsModel> &model,
                                             QObject *parent = nullptr);

public slots:
    void addSubnet(QString subnet);
    void removeSubnet(int index);
    void removeSubnets();

signals:
    void errorOccurred(const QString &errorMessage);
    void finished(const QString &message);

private:
    std::shared_ptr<Settings> m_settings;
    QSharedPointer<LocalSubnetExclusionsModel> m_model;
};

#endif // LOCALSUBNETEXCLUSIONSCONTROLLER_H
