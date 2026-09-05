#ifndef VPNCONFIGIRATIONSCONTROLLER_H
#define VPNCONFIGIRATIONSCONTROLLER_H

#include <QObject>

#include "configurators/configurator_base.h"
#include "containers/containers_defs.h"
#include "core/defs.h"
#include "settings.h"

class VpnConfigurationsController : public QObject
{
    Q_OBJECT
public:
    explicit VpnConfigurationsController(const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);

public slots:
    QJsonObject createVpnConfiguration(const QPair<QString, QString> &dns, const QJsonObject &serverConfig,
                                       const QJsonObject &containerConfig, const DockerContainer container);

signals:

private:
    QScopedPointer<ConfiguratorBase> createConfigurator(const Proto protocol);

    std::shared_ptr<Settings> m_settings;
};

#endif // VPNCONFIGIRATIONSCONTROLLER_H
