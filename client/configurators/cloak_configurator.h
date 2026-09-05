#ifndef CLOAK_CONFIGURATOR_H
#define CLOAK_CONFIGURATOR_H

#include <QObject>

#include "configurator_base.h"

using namespace amnezia;

class CloakConfigurator : public ConfiguratorBase
{
    Q_OBJECT
public:
    CloakConfigurator(std::shared_ptr<Settings> settings, QObject *parent = nullptr);
};

#endif // CLOAK_CONFIGURATOR_H
