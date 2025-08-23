#ifndef FRKNAPICONTROLLER_H
#define FRKNAPICONTROLLER_H

#include <settings.h>

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

namespace frkn {

class FrknApiController : public QObject {
  Q_OBJECT

public:
  explicit FrknApiController(std::shared_ptr<Settings> settings,
                             QObject *parent = nullptr);

  QString serverErrorMessage() const;

public slots:
  void registerUser(const QString &mnemonic);
  void loginUser(const QString &mnemonic);
  void connectUser(const QString &token);
  bool checkForUpdates();

signals:
  void registerFinished(const QString &message);
  void loginFinished(const QString &message, const QString &token);
  void connectFinished(const QString &message, const QString &subscriptionUrl,
                       bool beta);

private slots:
  void onRegisterReply(QNetworkReply *reply);
  void onLoginReply(QNetworkReply *reply);
  void onConnectReply(QNetworkReply *reply);

private:
  QUrl getApiUrl(const QString &path) const;

  QNetworkAccessManager *m_networkManager;
  std::shared_ptr<Settings> m_settings;
  QString m_domain;
};

} // namespace frkn

#endif // FRKNAPICONTROLLER_H