#include "meshBridgeWrapper.h"

#include <Dopamine-Swift.h>

namespace
{
    void onMeshMessage(const char *nickname, const char *content)
    {
        MeshBridgeWrapper *wrapper = MeshBridgeWrapper::instance();
        if (wrapper->onMessage) {
            wrapper->onMessage(QString::fromUtf8(nickname ? nickname : ""), QString::fromUtf8(content ? content : ""));
        }
    }

    void onMeshPrivateMessage(const char *senderFingerprint, const char *content)
    {
        MeshBridgeWrapper *wrapper = MeshBridgeWrapper::instance();
        if (wrapper->onPrivateMessage) {
            wrapper->onPrivateMessage(QString::fromUtf8(senderFingerprint ? senderFingerprint : ""),
                                      QString::fromUtf8(content ? content : ""));
        }
    }

    void onMeshPeerCount(int32_t count)
    {
        MeshBridgeWrapper *wrapper = MeshBridgeWrapper::instance();
        if (wrapper->onPeerCountChanged) {
            wrapper->onPeerCountChanged(static_cast<int>(count));
        }
    }
}

MeshBridgeWrapper *MeshBridgeWrapper::instance()
{
    static MeshBridgeWrapper *instance = new MeshBridgeWrapper();
    return instance;
}

void MeshBridgeWrapper::setNickname(const QString &nickname)
{
    Dopamine::meshSetNickname(nickname.toUtf8().constData());
}

void MeshBridgeWrapper::start()
{
    Dopamine::meshSetMessageCallback(reinterpret_cast<void *>(&onMeshMessage));
    Dopamine::meshSetPrivateMessageCallback(reinterpret_cast<void *>(&onMeshPrivateMessage));
    Dopamine::meshSetPeerCountCallback(reinterpret_cast<void *>(&onMeshPeerCount));
    Dopamine::meshStart();
}

void MeshBridgeWrapper::stop()
{
    Dopamine::meshStop();
    Dopamine::meshSetMessageCallback(nullptr);
    Dopamine::meshSetPrivateMessageCallback(nullptr);
    Dopamine::meshSetPeerCountCallback(nullptr);
}

void MeshBridgeWrapper::send(const QString &text)
{
    Dopamine::meshSend(text.toUtf8().constData());
}

bool MeshBridgeWrapper::sendTo(const QString &fingerprint, const QString &text)
{
    return Dopamine::meshSendTo(fingerprint.toUtf8().constData(), text.toUtf8().constData());
}

int MeshBridgeWrapper::peerCount()
{
    return Dopamine::meshPeerCount();
}

QString MeshBridgeWrapper::myId()
{
    return QString::fromStdString(Dopamine::meshMyId());
}

QString MeshBridgeWrapper::myPubkey()
{
    return QString::fromStdString(Dopamine::meshMyPubkey());
}

QString MeshBridgeWrapper::mySigningPubkey()
{
    return QString::fromStdString(Dopamine::meshMySigningPubkey());
}

QString MeshBridgeWrapper::sign(const QString &canonical)
{
    return QString::fromStdString(Dopamine::meshSign(canonical.toUtf8().constData()));
}

QString MeshBridgeWrapper::sealTo(const QString &recipientPubkeyHex, const QString &text)
{
    return QString::fromStdString(Dopamine::meshSealTo(recipientPubkeyHex.toUtf8().constData(), text.toUtf8().constData()));
}

QString MeshBridgeWrapper::unseal(const QString &envelopeB64)
{
    return QString::fromStdString(Dopamine::meshUnseal(envelopeB64.toUtf8().constData()));
}

QString MeshBridgeWrapper::peersJson()
{
    return QString::fromStdString(Dopamine::meshPeersJson());
}
