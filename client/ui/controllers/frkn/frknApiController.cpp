#include <ui/controllers/frkn/frknApiController.h>

#include <QCryptographicHash>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScopeGuard>
#include <QTimer>

namespace {

const QString ServerNotAvailableMessage =
    QObject::tr("Server not available. Please try again later.");

QString generateSha3_512(const QString &input) {
  QByteArray byteArray = input.toUtf8();
  QByteArray hash =
      QCryptographicHash::hash(byteArray, QCryptographicHash::Sha3_512);
  return QString(hash.toHex());
}

QString selectDomain() {
  QList domains{
      "frkn.org",
      "fr-dm.ru",
  };

  QEventLoop loop;
  QNetworkAccessManager manager;
  QNetworkReply *reply = nullptr;
  auto replyGuard = qScopeGuard([&reply]() {
    if (reply)
      reply->deleteLater();
  });

  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

  for (const auto &domain : domains) {
    QUrl url(QString("https://%1").arg(domain));
    QNetworkRequest request(url);
    reply = manager.get(request);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(5000); // 5 seconds timeout
    loop.exec();
    if (timer.isActive())
      timer.stop();
    if (reply->error() == QNetworkReply::NoError)
      return domain;
  }

  return QString();
}

} // namespace

namespace frkn {

FrknApiController::FrknApiController(std::shared_ptr<Settings> settings,
                                     QObject *parent)
    : QObject(parent), m_settings(settings),
      m_networkManager(new QNetworkAccessManager(this)),
      m_domain(selectDomain()) {
  qDebug() << "Selected domain:" << m_domain;
}

void FrknApiController::registerUser(const QString &mnemonic) {
  if (m_domain.isEmpty()) {
    emit registerFinished(ServerNotAvailableMessage);
    return;
  }

  QUrl url(getApiUrl("api/register"));
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
  if (m_domain.isEmpty()) {
    emit loginFinished(ServerNotAvailableMessage, QString());
    return;
  }

  QUrl url(getApiUrl("api/login"));
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
  if (m_domain.isEmpty()) {
    emit connectFinished(ServerNotAvailableMessage, QString(), false);
    return;
  }

  QUrl url(getApiUrl("api/connect"));
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", token.toUtf8());

  QNetworkReply *reply = m_networkManager->get(request);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { onConnectReply(reply); });
}

bool FrknApiController::checkForUpdates() {
  if (m_domain.isEmpty()) {
    return false;
  }

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

QUrl FrknApiController::getApiUrl(const QString &path) const {
  return QUrl(QString("https://%1/%2").arg(m_domain, path));
}

} // namespace frkn