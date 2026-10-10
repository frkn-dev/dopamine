#ifndef MESHBRIDGEWRAPPER_H
#define MESHBRIDGEWRAPPER_H

#include <QString>
#include <functional>

// C++ seam over the Swift MeshBridge (see platforms/ios/MeshBridge.swift).
// Implementation lives in meshBridgeWrapper.mm (ObjC++); iOS-only for now.
class MeshBridgeWrapper
{
public:
    static MeshBridgeWrapper *instance();

    void setNickname(const QString &nickname);
    void start();
    void stop();
    void send(const QString &text);
    bool sendTo(const QString &fingerprint, const QString &text);
    int peerCount();
    QString myId();
    QString myPubkey();
    QString mySigningPubkey();
    QString sign(const QString &canonical);
    QString sealTo(const QString &recipientPubkeyHex, const QString &text);
    QString unseal(const QString &envelopeB64);
    QString peersJson();

    // called on the main thread: public (nickname, text), private (senderFingerprint, text), (peerCount)
    std::function<void(const QString &, const QString &)> onMessage;
    std::function<void(const QString &, const QString &)> onPrivateMessage;
    std::function<void(int)> onPeerCountChanged;
};

#endif // MESHBRIDGEWRAPPER_H
