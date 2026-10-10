#include "meshBridgeWrapper.h"

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>

#include "android_utils.h"

namespace
{
    constexpr auto MESH_BRIDGE_CLASS = "org/amnezia/vpn/mesh/MeshBridge";

    // native methods may arrive on the Android UI thread; mesh callbacks touch
    // QML-bound state, so always hop to the Qt thread
    void onMeshMessage(JNIEnv *env, jclass, jstring nickname, jstring content)
    {
        const QString nick = QJniObject(nickname).toString();
        const QString text = QJniObject(content).toString();
        QMetaObject::invokeMethod(qApp, [nick, text]() {
            MeshBridgeWrapper *wrapper = MeshBridgeWrapper::instance();
            if (wrapper->onMessage) {
                wrapper->onMessage(nick, text);
            }
        }, Qt::QueuedConnection);
    }

    void onMeshPrivateMessage(JNIEnv *env, jclass, jstring senderFingerprint, jstring content)
    {
        const QString fingerprint = QJniObject(senderFingerprint).toString();
        const QString text = QJniObject(content).toString();
        QMetaObject::invokeMethod(qApp, [fingerprint, text]() {
            MeshBridgeWrapper *wrapper = MeshBridgeWrapper::instance();
            if (wrapper->onPrivateMessage) {
                wrapper->onPrivateMessage(fingerprint, text);
            }
        }, Qt::QueuedConnection);
    }

    void onMeshPeerCount(JNIEnv *env, jclass, jint count)
    {
        QMetaObject::invokeMethod(qApp, [count]() {
            MeshBridgeWrapper *wrapper = MeshBridgeWrapper::instance();
            if (wrapper->onPeerCountChanged) {
                wrapper->onPeerCountChanged(static_cast<int>(count));
            }
        }, Qt::QueuedConnection);
    }

    bool registerNativeMethods()
    {
        static bool registered = false;
        if (registered) {
            return true;
        }
        const JNINativeMethod methods[] = {
            { "onMessage", "(Ljava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void *>(onMeshMessage) },
            { "onPrivateMessage", "(Ljava/lang/String;Ljava/lang/String;)V", reinterpret_cast<void *>(onMeshPrivateMessage) },
            { "onPeerCount", "(I)V", reinterpret_cast<void *>(onMeshPeerCount) }
        };
        QJniEnvironment env;
        registered = env.registerNativeMethods(MESH_BRIDGE_CLASS, methods, sizeof(methods) / sizeof(JNINativeMethod));
        if (!registered) {
            qCritical() << "Failed to register mesh bridge native methods";
        }
        return registered;
    }

    QString callStringMethod(const char *methodName, const char *signature)
    {
        const QJniObject result = QJniObject::callStaticMethod<jstring>(MESH_BRIDGE_CLASS, methodName, signature);
        return result.toString();
    }

    QString callStringMethod(const char *methodName, const char *signature, const QString &arg)
    {
        const QJniObject result = QJniObject::callStaticMethod<jstring>(MESH_BRIDGE_CLASS, methodName, signature,
                                                                      QJniObject::fromString(arg).object<jstring>());
        return result.toString();
    }
}

MeshBridgeWrapper *MeshBridgeWrapper::instance()
{
    static MeshBridgeWrapper *instance = new MeshBridgeWrapper();
    return instance;
}

void MeshBridgeWrapper::setNickname(const QString &nickname)
{
    QJniObject::callStaticMethod<void>(MESH_BRIDGE_CLASS, "setNickname", "(Ljava/lang/String;)V",
                                       QJniObject::fromString(nickname).object<jstring>());
}

void MeshBridgeWrapper::start()
{
    if (!registerNativeMethods()) {
        return;
    }
    QJniObject activity = AndroidUtils::getActivity();
    Q_ASSERT(activity.isValid());
    QJniObject::callStaticMethod<void>(MESH_BRIDGE_CLASS, "start", "(Landroid/content/Context;)V", activity.object());
}

void MeshBridgeWrapper::stop()
{
    QJniObject::callStaticMethod<void>(MESH_BRIDGE_CLASS, "stop", "()V");
}

void MeshBridgeWrapper::send(const QString &text)
{
    QJniObject::callStaticMethod<void>(MESH_BRIDGE_CLASS, "send", "(Ljava/lang/String;)V",
                                       QJniObject::fromString(text).object<jstring>());
}

bool MeshBridgeWrapper::sendTo(const QString &fingerprint, const QString &text)
{
    return QJniObject::callStaticMethod<jboolean>(MESH_BRIDGE_CLASS, "sendTo",
                                                  "(Ljava/lang/String;Ljava/lang/String;)Z",
                                                  QJniObject::fromString(fingerprint).object<jstring>(),
                                                  QJniObject::fromString(text).object<jstring>());
}

int MeshBridgeWrapper::peerCount()
{
    return QJniObject::callStaticMethod<jint>(MESH_BRIDGE_CLASS, "peerCount", "()I");
}

QString MeshBridgeWrapper::myId()
{
    return callStringMethod("myFingerprint", "()Ljava/lang/String;");
}

QString MeshBridgeWrapper::myPubkey()
{
    return callStringMethod("myPubkey", "()Ljava/lang/String;");
}

QString MeshBridgeWrapper::mySigningPubkey()
{
    return callStringMethod("mySigningPubkey", "()Ljava/lang/String;");
}

QString MeshBridgeWrapper::sign(const QString &canonical)
{
    return callStringMethod("sign", "(Ljava/lang/String;)Ljava/lang/String;", canonical);
}

QString MeshBridgeWrapper::sealTo(const QString &recipientPubkeyHex, const QString &text)
{
    const QJniObject result = QJniObject::callStaticMethod<jstring>(MESH_BRIDGE_CLASS, "sealTo",
                                                                    "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                                                                    QJniObject::fromString(recipientPubkeyHex).object<jstring>(),
                                                                    QJniObject::fromString(text).object<jstring>());
    return result.toString();
}

QString MeshBridgeWrapper::unseal(const QString &envelopeB64)
{
    return callStringMethod("unseal", "(Ljava/lang/String;)Ljava/lang/String;", envelopeB64);
}

QString MeshBridgeWrapper::peersJson()
{
    return callStringMethod("peersJson", "()Ljava/lang/String;");
}
