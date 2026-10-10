#include "meshNetClient.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>

#include "dopamine_application.h"

MeshNetClient::MeshNetClient(QObject *parent) : QObject(parent)
{
}

void MeshNetClient::setBaseUrl(const QString &baseUrl)
{
    if (!baseUrl.isEmpty()) {
        m_baseUrl = baseUrl;
    }
}

void MeshNetClient::markOnline(bool online)
{
    if (m_online == online) {
        return;
    }
    m_online = online;
    emit onlineChanged(online);
}

QNetworkReply *MeshNetClient::post(const QString &path, const QJsonObject &body, int transferTimeoutMs,
                                   std::function<void(int, const QJsonObject &)> onDone)
{
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(
                this, [this, path, body, transferTimeoutMs, onDone]() { post(path, body, transferTimeoutMs, onDone); },
                Qt::QueuedConnection);
        return nullptr;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    if (transferTimeoutMs > 0) {
        request.setTransferTimeout(transferTimeoutMs);
    }

    QNetworkReply *reply = amnApp->networkManager()->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply, onDone]() {
        reply->deleteLater();
        const QByteArray responseData = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            markOnline(false);
            qWarning() << "[MESH] request failed:" << reply->errorString();
            onDone(-1, QJsonObject());
            return;
        }
        if (status > 0) {
            markOnline(true);
        }
        onDone(status > 0 ? status : -1, QJsonDocument::fromJson(responseData).object());
    });
    return reply;
}

void MeshNetClient::registerDevice(const QString &subscriptionId, const QString &noisePubkeyHex,
                                   const QString &signingPubkeyHex, const QString &name)
{
    QJsonObject body;
    body[QStringLiteral("subscription_id")] = subscriptionId;
    body[QStringLiteral("subscription_secret")] = subscriptionId;
    body[QStringLiteral("noise_pubkey")] = noisePubkeyHex;
    body[QStringLiteral("signing_pubkey")] = signingPubkeyHex;
    if (!name.isEmpty()) {
        body[QStringLiteral("name")] = name;
    }
    post(QStringLiteral("/register"), body, 30000, [this](int status, const QJsonObject &response) {
        const QString uin = response.value(QStringLiteral("uin")).toString();
        if (status == 200 && !uin.isEmpty()) {
            emit registered(uin);
        } else {
            emit registerFailed(status);
        }
    });
}

void MeshNetClient::lookup(const QString &uin)
{
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, [this, uin]() { lookup(uin); }, Qt::QueuedConnection);
        return;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + QStringLiteral("/lookup/") + uin));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(30000);

    QNetworkReply *reply = amnApp->networkManager()->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, uin]() {
        reply->deleteLater();
        const QByteArray responseData = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            markOnline(false);
            emit lookupFinished(uin, false, false, QString(), QStringList());
            return;
        }
        markOnline(true);
        const QJsonObject response = QJsonDocument::fromJson(responseData).object();
        QStringList devices;
        const QJsonArray array = response.value(QStringLiteral("devices")).toArray();
        for (const auto &value : array) {
            const QString pubkey = value.toObject().value(QStringLiteral("noise_pubkey")).toString();
            if (!pubkey.isEmpty()) {
                devices.append(pubkey);
            }
        }
        if (devices.isEmpty()) {
            const QString legacy = response.value(QStringLiteral("noise_pubkey")).toString();
            if (!legacy.isEmpty()) {
                devices.append(legacy);
            }
        }
        emit lookupFinished(uin, status == 200 && !devices.isEmpty(), response.value(QStringLiteral("online")).toBool(),
                            response.value(QStringLiteral("name")).toString(), devices);
    });
}

void MeshNetClient::send(const QString &fromUin, const QString &toUin, const QJsonArray &envelopes)
{
    if (!m_signFn || envelopes.isEmpty()) {
        emit sendFinished(toUin, false);
        return;
    }
    const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
    QStringList ciphertexts;
    for (const auto &value : envelopes) {
        ciphertexts.append(value.toObject().value(QStringLiteral("ciphertext")).toString());
    }
    const QString canonical = QStringLiteral("send\n") + toUin + QStringLiteral("\n") + ts + QStringLiteral("\n")
            + ciphertexts.join(QStringLiteral("\n"));
    const QString sig = m_signFn(canonical);
    if (sig.isEmpty()) {
        emit sendFinished(toUin, false);
        return;
    }

    QJsonObject body;
    body[QStringLiteral("from_uin")] = fromUin;
    body[QStringLiteral("to_uin")] = toUin;
    body[QStringLiteral("envelopes")] = envelopes;
    body[QStringLiteral("ts")] = ts.toLongLong();
    body[QStringLiteral("sig")] = sig;
    post(QStringLiteral("/send"), body, 30000, [this, toUin](int status, const QJsonObject &) {
        emit sendFinished(toUin, status == 200);
    });
}

void MeshNetClient::setDisplayName(const QString &uin, const QString &name)
{
    if (!m_signFn || uin.isEmpty()) {
        return;
    }
    const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
    const QString canonical =
            QStringLiteral("name\n") + uin + QStringLiteral("\n") + ts + QStringLiteral("\n") + name;
    const QString sig = m_signFn(canonical);
    if (sig.isEmpty()) {
        return;
    }
    QJsonObject body;
    body[QStringLiteral("uin")] = uin;
    body[QStringLiteral("name")] = name;
    body[QStringLiteral("ts")] = ts.toLongLong();
    body[QStringLiteral("sig")] = sig;
    post(QStringLiteral("/name"), body, 30000, [](int, const QJsonObject &) {});
}

void MeshNetClient::startPolling(const QString &uin)
{
    m_pollUin = uin;
    m_pollWanted = !uin.isEmpty();
    if (m_pollWanted) {
        pollOnce();
    }
}

void MeshNetClient::stopPolling()
{
    m_pollWanted = false;
    m_pollUin.clear();
    if (m_pollWatchdog) {
        m_pollWatchdog->stop();
    }
    if (m_pollInFlight && m_pollReply) {
        m_pollReply->abort(); // finished handler sees m_pollWanted=false and bails
    }
}

void MeshNetClient::schedulePoll(int delayMs)
{
    if (!m_pollWanted) {
        return;
    }
    QTimer::singleShot(delayMs, this, [this]() { pollOnce(); });
}

void MeshNetClient::pollOnce()
{
    if (!m_pollWanted || m_pollUin.isEmpty() || !m_signFn || m_pollInFlight) {
        return;
    }
    const QString uin = m_pollUin;
    const QString ts = QString::number(QDateTime::currentSecsSinceEpoch());
    const QString canonical = QStringLiteral("poll\n") + uin + QStringLiteral("\n") + ts;
    const QString sig = m_signFn(canonical);
    if (sig.isEmpty()) {
        schedulePoll(5000);
        return;
    }
    QJsonObject body;
    body[QStringLiteral("uin")] = uin;
    body[QStringLiteral("ts")] = ts.toLongLong();
    body[QStringLiteral("sig")] = sig;
    m_pollInFlight = true;
    // poll holds the connection up to 30s server-side with no transfer timeout,
    // so a silently dead socket (network hop, tunnel flap) would wedge the loop
    // forever — abort it ourselves past 40s and let the loop resume
    m_pollReply = post(QStringLiteral("/poll"), body, 0, [this](int status, const QJsonObject &response) {
        m_pollInFlight = false;
        m_pollReply = nullptr;
        if (m_pollWatchdog) {
            m_pollWatchdog->stop();
        }
        if (!m_pollWanted) {
            return;
        }
        if (status != 200) {
            emit pullFinished(QVariantList());
            schedulePoll(5000);
            return;
        }
        QVariantList messages;
        const QJsonArray array = response.value(QStringLiteral("messages")).toArray();
        for (const auto &value : array) {
            const QJsonObject object = value.toObject();
            QVariantMap message;
            message[QStringLiteral("from_uin")] = object.value(QStringLiteral("from_uin")).toString();
            message[QStringLiteral("ciphertext")] = object.value(QStringLiteral("ciphertext")).toString();
            messages.append(message);
        }
        emit pullFinished(messages);
        schedulePoll(0);
    });
    if (!m_pollWatchdog) {
        m_pollWatchdog = new QTimer(this);
        m_pollWatchdog->setSingleShot(true);
        connect(m_pollWatchdog, &QTimer::timeout, this, [this]() {
            if (m_pollInFlight && m_pollReply) {
                qWarning() << "[MESH] poll stalled, aborting";
                m_pollReply->abort();
            }
        });
    }
    m_pollWatchdog->start(40000);
}
