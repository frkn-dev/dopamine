#ifndef INSTALLCONTROLLER_H
#define INSTALLCONTROLLER_H

#include <QObject>

#include "containers/containers_defs.h"
#include "core/defs.h"
#include "ui/models/servers_model.h"

class InstallController : public QObject
{
    Q_OBJECT
public:
    explicit InstallController(const QSharedPointer<ServersModel> &serversModel,
                               const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);

public slots:
    void removeProcessedServer();
    void removeApiConfig(const int serverIndex);

    QRegularExpression ipAddressRegExp();

    void validateConfig();

signals:
    void configValidated(bool isValid);
    void removeProcessedServerFinished(const QString &finishedMessage);

    void installationErrorOccurred(ErrorCode errorCode);
    void apiConfigRemoved(const QString &message);

    void noInstalledContainers();

private:
    QSharedPointer<ServersModel> m_serversModel;

    std::shared_ptr<Settings> m_settings;
};

#endif // INSTALLCONTROLLER_H
