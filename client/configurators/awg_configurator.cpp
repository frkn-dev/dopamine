#include "awg_configurator.h"

AwgConfigurator::AwgConfigurator(std::shared_ptr<Settings> settings, QObject *parent)
    : WireguardConfigurator(settings, parent)
{
}
