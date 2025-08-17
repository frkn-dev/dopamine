#include <ui/controllers/frkn/frknApiController.h>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

QString generateSha3_512(const QString &input) {
  QByteArray byteArray = input.toUtf8();
  QByteArray hash =
      QCryptographicHash::hash(byteArray, QCryptographicHash::Sha3_512);
  return QString(hash.toHex());
}

} // namespace

namespace frkn {

FrknApiController::FrknApiController(std::shared_ptr<Settings> settings,
                                     QObject *parent)
    : QObject(parent), m_settings(settings),
      m_networkManager(new QNetworkAccessManager(this)) {}

void FrknApiController::registerUser(const QString &mnemonic) {
  QUrl url("https://frkn.org/api/register");
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QJsonObject json;
  json["password"] = generateSha3_512(mnemonic);

  QNetworkReply *reply =
      m_networkManager->post(request, QJsonDocument(json).toJson());
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onRegisterReply(reply); });
}

void FrknApiController::loginUser(const QString &mnemonic) {
  QUrl url("https://frkn.org/api/login");
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QJsonObject json;
  json["password"] = generateSha3_512(mnemonic);

  QNetworkReply *reply =
      m_networkManager->post(request, QJsonDocument(json).toJson());
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onLoginReply(reply); });
}

void FrknApiController::connectUser(const QString &token) {
  QUrl url("https://frkn.org/api/connect");
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", token.toUtf8());

  QNetworkReply *reply = m_networkManager->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onConnectReply(reply); });
}

bool FrknApiController::checkForUpdates() {
  qDebug() << "Checking for updates";
  QString token = m_settings->frknToken();
  if (!token.isEmpty()) {
    connectUser(token);
    return true;
  } else {
    qWarning() << "No token found, skipping update check";
  }
  return false;
}

void FrknApiController::onRegisterReply(QNetworkReply *reply) {
  QString message;
  if (reply->error() == QNetworkReply::NoError) {
    QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
    QJsonObject jsonObject = jsonResponse.object();
    QString status = jsonObject["status"].toString();
    if (status == "error") {
      message = jsonObject["message"].toString();
      QVariant statusCode =
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
      int httpStatusCode = statusCode.isValid() ? statusCode.toInt() : 200;
      if (message.isEmpty() && httpStatusCode == 500) {
        message = "Wrong mnemonic";
      }
    }
  } else {
    message = reply->errorString();
  }
  emit registerFinished(message);
  reply->deleteLater();
}

void FrknApiController::onLoginReply(QNetworkReply *reply) {
  QString message;
  QString token;
  if (reply->error() == QNetworkReply::NoError) {
    QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
    QJsonObject jsonObject = jsonResponse.object();
    QString status = jsonObject["status"].toString();
    if (status == "success") {
      token = jsonObject["token"].toString();
    } else {
      message = jsonObject["message"].toString();
    }
  } else {
    message = reply->errorString();
  }
  emit loginFinished(message, token);
  reply->deleteLater();
}

void FrknApiController::onConnectReply(QNetworkReply *reply) {
  QString message;
  QString subscriptionUrl;
  bool beta = false;
  if (reply->error() == QNetworkReply::NoError) {
    QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
    QJsonObject jsonObject = jsonResponse.object();
    QString status = jsonObject["status"].toString();
    if (status == "active") {
      subscriptionUrl = jsonObject["subscription_url"].toString();
      beta = jsonObject["beta"].toBool();
    } else if (status == "error") {
      message = jsonObject["message"].toString();
    }
  } else {
    message = reply->errorString();
  }
  emit connectFinished(message, subscriptionUrl, beta);
  reply->deleteLater();
}

} // namespace frkn