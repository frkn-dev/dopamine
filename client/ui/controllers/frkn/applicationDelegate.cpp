#include "ui/controllers/frkn/applicationDelegate.h"

#include "amnezia_application.h"
#include <qobject.h>

namespace frkn {
ApplicationDelegate::ApplicationDelegate(AmneziaApplication *app,
                                         QObject *parent)
    : QObject(parent), m_app(app) {}

void ApplicationDelegate::registerTypes() {
  m_bip39Helper.reset(new Bip39Helper());
  qmlRegisterSingletonInstance("Bip39Helper", 1, 0, "Bip39Helper",
                               m_bip39Helper.get());
}

void ApplicationDelegate::initControllers() {
  m_frknApiController.reset(new frkn::FrknApiController(m_app->m_settings));
  m_app->m_engine->rootContext()->setContextProperty("FrknApi",
                                                     m_frknApiController.get());
  connect(m_frknApiController.get(), &frkn::FrknApiController::loginFinished,
          this, [this](const QString &message, const QString &token) {
            qDebug() << "Login finished" << message << token;
            m_app->m_settings->setFrknToken(token);
            m_frknApiController->connectUser(token);
          });

  m_frknConfigController.reset(new frkn::ConfigController());
  connect(m_frknApiController.get(), &frkn::FrknApiController::connectFinished,
          [this](const QString &message, const QString &subscriptionUrl) {
            qDebug() << "Connect finished" << message << subscriptionUrl;
            if (!subscriptionUrl.isEmpty()) {
              m_frknConfigController->loadConfig(subscriptionUrl);
            }
          });

  connect(m_frknConfigController.get(), &frkn::ConfigController::configReceived,
          [this](const QStringList &configs) {
            qDebug() << "Configs received: " << configs.size();

            m_app->m_settings->setLastUpdateCheck(
                QDateTime::currentDateTimeUtc());

            QList<QJsonObject> servers;
            for (const auto &config : configs) {
              if (m_app->m_importController->extractConfigFromData(config)) {
                servers.append(m_app->m_importController->getJsonConfig());
              }
            }

            for (auto &server : servers) {
              QString description = server["description"].toString();
              QRegularExpression re("([A-Z]{2}.*)");
              QRegularExpressionMatch match = re.match(description);
              if (match.hasMatch()) {
                description = match.captured(1);
                server["description"] = description.trimmed();
              }
              QJsonObject apiConfig;
              apiConfig["server_country_code"] = description.left(2);
              apiConfig["server_country_name"] = description.left(2);
              server["api_config"] = apiConfig;
            }

            std::sort(servers.begin(), servers.end(),
                      [](const QJsonObject &a, const QJsonObject &b) {
                        QString descA = a["description"].toString();
                        QString descB = b["description"].toString();
                        return descA < descB;
                      });

            // Trying to save previously selected server since users prefer to
            // keep selected country.
            const QString serverName =
                m_app->m_serversModel->getDefaultServerName();
            auto newServerIndex = 0;
            for (int i = 0; i < servers.size(); ++i) {
              if (servers[i]["description"].toString() == serverName) {
                newServerIndex = i;
                break;
              }
            }

            m_app->m_serversModel->removeServers();
            m_app->m_serversModel->addServers(servers);
            m_app->m_serversModel->setDefaultServerIndex(newServerIndex);
            emit m_app->m_importController->importFinished();
            m_app->m_pageController->showBusyIndicator(false);
          });

  connect(m_frknConfigController.get(), &frkn::ConfigController::loadError,
          [this](const QString &error) {
            qWarning() << "Config load error: " << error;
            m_app->m_pageController->showBusyIndicator(false);
          });
}

void ApplicationDelegate::updateConfigs() {
  if (m_frknApiController->checkForUpdates()) {
    m_app->m_pageController->showBusyIndicator(true);
  }
}

} // namespace frkn