#include "installController.h"

#include <QJsonArray>
#include <QJsonObject>

#include "core/api/apiUtils.h"
#include "core/networkUtilities.h"

InstallController::InstallController(const QSharedPointer<ServersModel> &serversModel,
                                     const std::shared_ptr<Settings> &settings, QObject *parent)
    : QObject(parent), m_serversModel(serversModel), m_settings(settings)
{
}

void InstallController::removeProcessedServer()
{
    int serverIndex = m_serversModel->getProcessedServerIndex();
    QString serverName = m_serversModel->data(serverIndex, ServersModel::Roles::NameRole).toString();

    m_serversModel->removeServer();
    emit removeProcessedServerFinished(tr("Server '%1' was removed").arg(serverName));
}

void InstallController::removeApiConfig(const int serverIndex)
{
    m_serversModel->removeApiConfig(serverIndex);
    emit apiConfigRemoved(tr("Api config removed"));
}

QRegularExpression InstallController::ipAddressRegExp()
{
    return NetworkUtilities::ipAddressRegExp();
}

void InstallController::validateConfig()
{
    int serverIndex = m_serversModel->getDefaultServerIndex();
    QJsonObject serverConfigObject = m_serversModel->getServerConfig(serverIndex);

    if (apiUtils::isServerFromApi(serverConfigObject)) {
        emit configValidated(true);
        return;
    }

    if (!m_serversModel->data(serverIndex, ServersModel::Roles::HasInstalledContainers).toBool()) {
        emit noInstalledContainers();
        emit configValidated(false);
        return;
    }

    DockerContainer container = qvariant_cast<DockerContainer>(m_serversModel->data(serverIndex, ServersModel::Roles::DefaultContainerRole));

    if (container == DockerContainer::None) {
        emit installationErrorOccurred(ErrorCode::NoInstalledContainersError);
        emit configValidated(false);
        return;
    }

    // the SSH/self-hosted flow is gone: a non-API (imported) config must already
    // carry a ready-made last_config for every protocol of its default container
    QJsonObject containerConfig;
    const auto containers = serverConfigObject.value(config_key::containers).toArray();
    for (const auto &containerEntry : containers) {
        const QJsonObject entry = containerEntry.toObject();
        if (ContainerProps::containerFromString(entry.value(config_key::container).toString()) == container) {
            containerConfig = entry;
            break;
        }
    }

    bool isValid = !containerConfig.isEmpty();
    for (Proto protocol : ContainerProps::protocolsForContainer(container)) {
        QString protocolConfig =
                containerConfig.value(ProtocolProps::protoToString(protocol)).toObject().value(config_key::last_config).toString();

        if (protocolConfig.isEmpty()) {
            isValid = false;
            break;
        }
    }

    emit configValidated(isValid);
}
