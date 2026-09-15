#include "keyActivationController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>

#include "dopamine_application.h"

namespace
{
    const QString keyApiBase = QStringLiteral("https://api.frkn.org/key");

    QJsonObject keyObjectFromResponse(const QByteArray &responseData)
    {
        const QJsonObject body = QJsonDocument::fromJson(responseData).object();
        return body.value(QStringLiteral("response"))
                .toObject()
                .value(QStringLiteral("instance"))
                .toObject()
                .value(QStringLiteral("Key"))
                .toObject();
    }
}

KeyActivationController::KeyActivationController(QObject *parent) : QObject(parent)
{
}

void KeyActivationController::validateKey(const QString &code)
{
    // Ensure we run on the Qt main thread (QNetworkAccessManager requires it)
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, code]() { validateKey(code); }, Qt::QueuedConnection);
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(keyApiBase + QStringLiteral("/validate?key=") + QUrl::toPercentEncoding(code)));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(15000);

    QNetworkReply *reply = amnApp->networkManager()->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, code, reply]() {
        reply->deleteLater();

        const QByteArray responseData = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qWarning() << "[KEY] validate failed:" << reply->errorString() << "status:" << status
                       << "response:" << responseData;
            if (status == 400 && responseData.contains("Key is not valid")) {
                emit keyErrorOccurred(tr("Check the key — looks like a typo"));
            } else if (status == 404) {
                emit keyErrorOccurred(tr("This key does not exist"));
            } else {
                emit keyErrorOccurred(tr("Connection failed, try again"));
            }
            return;
        }

        const QJsonObject key = keyObjectFromResponse(responseData);
        const QString subscriptionId = key.value(QStringLiteral("subscription_id")).toString();

        if (key.value(QStringLiteral("activated")).toBool()) {
            // reinstall case: silently import the already linked subscription
            if (!subscriptionId.isEmpty()) {
                emit keyAlreadyLinked(subscriptionId);
            } else {
                emit keyErrorOccurred(tr("Key is already activated"));
            }
            return;
        }

        const QString kind = key.value(QStringLiteral("kind")).toString();
        emit keyValidationPassed(code, key.value(QStringLiteral("days")).toInt(),
                                 key.value(QStringLiteral("traffic_gib")).toInt(), kind == QLatin1String("lite"));
    });
}

void KeyActivationController::activateKey(const QString &code, const QString &email)
{
    // Ensure we run on the Qt main thread (QNetworkAccessManager requires it)
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, code, email]() { activateKey(code, email); }, Qt::QueuedConnection);
        return;
    }

    QJsonObject body;
    body[QStringLiteral("code")] = code;
    body[QStringLiteral("subscription_id")] = QJsonValue::Null;
    body[QStringLiteral("email")] = email.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(email);

    QNetworkRequest request;
    request.setUrl(QUrl(keyApiBase + QStringLiteral("/activate")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(15000);

    QNetworkReply *reply = amnApp->networkManager()->post(request, QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const QByteArray responseData = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qWarning() << "[KEY] activate failed:" << reply->errorString() << "status:" << status
                       << "response:" << responseData;
            if (status == 400 && responseData.contains("email_required")) {
                emit emailRequired();
            } else if (status == 400 && responseData.contains("Key already activated")) {
                emit keyErrorOccurred(tr("Key is already activated"));
            } else if (status == 404) {
                emit keyErrorOccurred(tr("This key does not exist"));
            } else {
                emit keyErrorOccurred(tr("Connection failed, try again"));
            }
            return;
        }

        const QString subscriptionId = keyObjectFromResponse(responseData).value(QStringLiteral("subscription_id")).toString();
        if (subscriptionId.isEmpty()) {
            qWarning() << "[KEY] no subscription id in activate response:" << responseData;
            emit keyErrorOccurred(tr("Connection failed, try again"));
            return;
        }

        emit keyActivated(subscriptionId);
    });
}
