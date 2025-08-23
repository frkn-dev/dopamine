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

QString generateSha3_512(const QString &input) {
  QByteArray byteArray = input.toUtf8();
  QByteArray hash =
      QCryptographicHash::hash(byteArray, QCryptographicHash::Sha3_512);
  return QString(hash.toHex());
}

QString selectDomain(QString preferred = QString()) {
  QStringList domains{
      "frkn.org",
      "fr-dm.ru",
  };
  if (!preferred.isEmpty())
    domains.push_front(preferred);

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

  return domains.front();
}

} // namespace

namespace frkn {

FrknApiController::FrknApiController(std::shared_ptr<Settings> settings,
                                     QObject *parent)
    : QObject(parent), m_settings(settings),
      m_networkManager(new QNetworkAccessManager(this)),
      m_domain(selectDomain()) {
  qDebug() << "Selected domain:" << m_domain;
  m_networkManager->setTransferTimeout(10000);
}

QString FrknApiController::serverErrorMessage() const {
  return tr("Server not available. Please try again later.");
}

void FrknApiController::registerUser(const QString &mnemonic) {
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
  QUrl url(getApiUrl("api/connect"));
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

void FrknApiController::setDomain(const QString &domain) {
  m_domain = selectDomain(domain);
  qDebug() << "Domain changed to:" << m_domain;
}

void FrknApiController::onRegisterReply(QNetworkReply *reply) {
  QString message;
  if (reply->error() == QNetworkReply::NoError) {
    QJsonDocument jsonResponse = QJsonDocument::fromJson(reply->readAll());
    QJsonObject jsonObject = jsonResponse.object();
    QString status = jsonObject["status"].toString();
    if (status == "error") {
      qDebug() << "Registration error:" << jsonObject["message"].toString();
      message = jsonObject["message"].toString();
      QVariant statusCode =
          reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
      int httpStatusCode = statusCode.isValid() ? statusCode.toInt() : 200;
      if (message.isEmpty() && httpStatusCode == 500) {
        message = "Wrong mnemonic";
      }
    }
  } else {
    qDebug() << "Network error during registration:" << reply->errorString();
    message = serverErrorMessage();
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
      qDebug() << "Login error:" << message;
    }
  } else {
    qDebug() << "Network error during login:" << reply->errorString();
    message = serverErrorMessage();
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
      qDebug() << "Connect error:" << message;
    }
  } else {
    qDebug() << "Network error during connect:" << reply->errorString();
    message = serverErrorMessage();
  }
  emit connectFinished(message, subscriptionUrl, beta);
  reply->deleteLater();
}

QUrl FrknApiController::getApiUrl(const QString &path) const {
  return QUrl(QString("https://%1/%2").arg(m_domain, path));
}

} // namespace frkn