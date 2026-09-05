#ifndef XRAY_CONFIGURATOR_H
#define XRAY_CONFIGURATOR_H

#include <QObject>

#include "configurator_base.h"
#include "core/defs.h"

class XrayConfigurator : public ConfiguratorBase
{
    Q_OBJECT
public:
    XrayConfigurator(std::shared_ptr<Settings> settings, QObject *parent = nullptr);
};

#endif // XRAY_CONFIGURATOR_H
