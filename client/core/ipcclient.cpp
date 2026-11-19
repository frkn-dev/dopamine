#include "ipcclient.h"
#include "ipc.h"
#include <QRemoteObjectNode>
#include <QtNetwork/qlocalsocket.h>

IpcClient::IpcClient(QObject *parent) : QObject(parent)
{
    m_node.connectToNode(QUrl("local:" + amnezia::getIpcServiceUrl()));
    m_interface.reset(m_node.acquire<IpcInterfaceReplica>());
}

IpcClient& IpcClient::Instance()
{
    thread_local IpcClient ipcClient;
    return ipcClient;
}

QSharedPointer<IpcInterfaceReplica> IpcClient::Interface()
{
    IpcClient &self = Instance();
    QSharedPointer<IpcInterfaceReplica> rep = self.m_interface;
    // a dead replica never recovers on its own (daemon restarted: update,
    // crash) — reconnect the node and re-acquire, otherwise every later
    // IPC call silently no-ops
    if (rep.isNull() || !rep->isReplicaValid()) {
        self.m_node.connectToNode(QUrl("local:" + amnezia::getIpcServiceUrl()));
        rep.reset(self.m_node.acquire<IpcInterfaceReplica>());
        self.m_interface = rep;
    }
    if (rep.isNull()) {
        qCritical() << "IpcClient::Interface(): Failed to acquire replica";
        return nullptr;
    }
    if (!rep->waitForSource(1000)) {
        qCritical() << "IpcClient::Interface(): Failed to initialize replica";
        return nullptr;
    }
    if (!rep->isReplicaValid()) {
        qWarning() << "IpcClient::Interface(): Replica is invalid";
    }
    return rep;
}

QSharedPointer<IpcInterfaceReplica> IpcClient::readyInterface()
{
    IpcClient &self = Instance();
    QSharedPointer<IpcInterfaceReplica> rep = self.m_interface;
    if (rep.isNull()) {
        return nullptr;
    }
    // fast path: a valid replica is initialized, waitForSource() would return at once
    if (rep->isReplicaValid()) {
        return rep;
    }
    // nested call from the event loop of the wait below: fail fast, do not nest another loop
    if (self.m_waitingForSource) {
        return nullptr;
    }

    self.m_waitingForSource = true;
    const bool ready = rep->waitForSource(1000) && rep->isReplicaValid();
    self.m_waitingForSource = false;
    if (!ready) {
        return nullptr;
    }
    return rep;
}

QSharedPointer<IpcProcessInterfaceReplica> IpcClient::CreatePrivilegedProcess()
{
    return withInterface([](QSharedPointer<IpcInterfaceReplica> &iface) -> QSharedPointer<IpcProcessInterfaceReplica> {
        auto createPrivilegedProcess = iface->createPrivilegedProcess();
        if (!createPrivilegedProcess.waitForFinished()) {
            qCritical() << "Failed to create privileged process";
            return nullptr;
        }

        const int pid = createPrivilegedProcess.returnValue();

        auto* node = new QRemoteObjectNode();
        node->connectToNode(QUrl(QString("local:%1").arg(amnezia::getIpcProcessUrl(pid))));

        QSharedPointer<IpcProcessInterfaceReplica> rep(
            node->acquire<IpcProcessInterfaceReplica>(),
            [node] (IpcProcessInterfaceReplica *ptr) {
                delete ptr;
                node->deleteLater();
            }
        );
        if (rep.isNull()) {
            qCritical() << "IpcClient::CreatePrivilegedProcess(): Failed to acquire replica";
            return nullptr;
        }
        if (!rep->waitForSource()) {
            qCritical() << "IpcClient::CreatePrivilegedProcess(): Failed to initialize replica";
            return nullptr;
        }
        if (!rep->isReplicaValid()) {
            qCritical() << "IpcClient::CreatePrivilegedProcess(): Replica is invalid";
            return nullptr;
        }

        return rep;
    },
    []() -> QSharedPointer<IpcProcessInterfaceReplica> {
        return nullptr;
    });
}
