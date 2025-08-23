#include "configController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <qcontainerfwd.h>
#include <qnetworkreply.h>

namespace frkn {

ConfigController::ConfigController(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)) {
  m_networkManager->setTransferTimeout(10000);
}

void ConfigController::loadConfig(const QString &url) {
  QNetworkRequest request(QUrl{url});
  QNetworkReply *reply = m_networkManager->get(request);

  connect(reply, &QNetworkReply::finished,
          [this, reply]() { onLoadConfigReply(reply); });

  connect(reply, &QNetworkReply::errorOccurred,
          [this, reply](QNetworkReply::NetworkError error) {
            qDebug() << reply->errorString() << error;
            emit loadError(reply->errorString());
          });
  connect(reply, &QNetworkReply::sslErrors,
          [this, reply](const QList<QSslError> &errors) {
            qDebug().noquote() << errors;

            QString errorsString;
            for (const QSslError &error : errors) {
              errorsString += error.errorString() + '\n';
            }
            emit loadError(errorsString);
          });
}

void ConfigController::onLoadConfigReply(QNetworkReply *reply) {
  QStringList configs;
  if (reply->error() == QNetworkReply::NoError) {
    QByteArray responseData = reply->readAll();
    QByteArray decodedData = QByteArray::fromBase64(responseData);
    QString decodedString = QString::fromUtf8(decodedData);
    qDebug() << "Decoded config data:" << decodedString;
    QList<QString> lines = decodedString.split('\n');
    for (const QString &line : lines) {
      if (line.startsWith("vmess://") || line.startsWith("vless://")
          // || line.startsWith("ss://")
      ) {
        configs.push_back(line);
      }
    }
  }
  if (configs.empty()) {
    emit loadError(tr("Can't load config"));
  } else {
    emit configReceived(configs);
  }
  reply->deleteLater();
}

} // namespace frkn