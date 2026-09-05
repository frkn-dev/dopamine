#ifndef CONFIGURATORBASE_H
#define CONFIGURATORBASE_H

#include <QObject>

#include "settings.h"

class ConfiguratorBase : public QObject
{
    Q_OBJECT
public:
    explicit ConfiguratorBase(std::shared_ptr<Settings> settings, QObject *parent = nullptr);

    virtual QString processConfigWithLocalSettings(const QPair<QString, QString> &dns, const bool isApiConfig,
                                                   QString &protocolConfigString);
    virtual QString processConfigWithExportSettings(const QPair<QString, QString> &dns, const bool isApiConfig,
                                                    QString &protocolConfigString);

protected:
    void processConfigWithDnsSettings(const QPair<QString, QString> &dns, QString &protocolConfigString);

    std::shared_ptr<Settings> m_settings;
};

#endif // CONFIGURATORBASE_H
