#ifndef MESHNETCLIENT_H
#define MESHNETCLIENT_H

#include <QObject>
#include <QJsonArray>
#include <QStringList>
#include <QVariantList>

#include <functional>

class QNetworkReply;
class QTimer;

// HTTP client for the fcore mesh relay API (server-relayed transport for peers
// out of BLE range). The server only ever sees sealed ciphertext envelopes;
// signing is delegated to the platform identity (iOS: Secure Enclave-backed
// BitChat key via MeshBridgeWrapper) through setSignFunction().
class MeshNetClient : public QObject
{
    Q_OBJECT
public:
    explicit MeshNetClient(QObject *parent = nullptr);

    void setSignFunction(std::function<QString(const QString &canonical)> fn) { m_signFn = std::move(fn); }
    void setBaseUrl(const QString &baseUrl);

    void registerDevice(const QString &subscriptionId, const QString &noisePubkeyHex, const QString &signingPubkeyHex,
                        const QString &name);
    void lookup(const QString &uin);
    void send(const QString &fromUin, const QString &toUin, const QJsonArray &envelopes);
    void setDisplayName(const QString &uin, const QString &name);
    void startPolling(const QString &uin);
    void stopPolling();

    bool isOnline() const { return m_online; }

signals:
    void registered(const QString &uin);
    void registerFailed(int status);
    void lookupFinished(const QString &uin, bool found, bool online, const QString &name, const QStringList &devicePubkeys);
    void sendFinished(const QString &toUin, bool ok);
    void pullFinished(const QVariantList &messages); // [{from_uin, ciphertext}]
    void onlineChanged(bool online);

private:
    QNetworkReply *post(const QString &path, const QJsonObject &body, int transferTimeoutMs,
                        std::function<void(int status, const QJsonObject &response)> onDone);
    void pollOnce();
    void schedulePoll(int delayMs);
    void markOnline(bool online);

    QString m_baseUrl = QStringLiteral("https://mesh.frkn.app/v1/mesh");
    std::function<QString(const QString &)> m_signFn;
    bool m_online = false;
    bool m_pollWanted = false;
    bool m_pollInFlight = false;
    QString m_pollUin;
    QNetworkReply *m_pollReply = nullptr;
    QTimer *m_pollWatchdog = nullptr;
};

#endif // MESHNETCLIENT_H
