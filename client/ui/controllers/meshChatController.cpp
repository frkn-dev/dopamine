#include "meshChatController.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStandardPaths>

#include "meshNetClient.h"

#if defined(Q_OS_IOS)
#include "platforms/ios/meshBridgeWrapper.h"
#define MESH_BRIDGE_AVAILABLE 1
#elif defined(Q_OS_ANDROID)
#include "platforms/android/meshBridgeWrapper.h"
#define MESH_BRIDGE_AVAILABLE 1
#endif

namespace
{
    const int MAX_HISTORY_PER_CONTACT = 200;

    QString fingerprintFromPubkey(const QString &pubkeyHex)
    {
        return QString::fromLatin1(
                QCryptographicHash::hash(QByteArray::fromHex(pubkeyHex.toLatin1()), QCryptographicHash::Sha256).toHex());
    }

    QString contactLabel(const QVariantMap &contact)
    {
        const QString uin = contact.value(QStringLiteral("uin")).toString();
        const QString pet = contact.value(QStringLiteral("name")).toString();
        if (!pet.isEmpty() && pet != uin) {
            return pet;
        }
        const QString server = contact.value(QStringLiteral("serverName")).toString();
        if (!server.isEmpty()) {
            return server;
        }
        return uin;
    }

    QString historyFilePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/mesh-history.json");
    }
}

MeshChatController::MeshChatController(const std::shared_ptr<Settings> &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    // BLE interest held while the app is backgrounded makes iOS keep the
    // registration and pester the user with "accessory would like to open"
    // wake prompts; it also burns battery. Stop mesh on background, resume
    // on foreground if it was running.
    // iOS fires Inactive for both a transient banner AND screen lock (with
    // Hidden/Suspended following late or never): stop BLE with a grace period
    // on Inactive so locking the phone actually drops our BLE registration —
    // otherwise iOS keeps it and nags with "accessory would like to open"
    // alerts on the lock screen. Banners cancel via the quick Active return.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            m_inactiveStopTimer.stop();
            if (m_resumeOnForeground) {
                m_resumeOnForeground = false;
                startMesh();
            }
            return;
        }
        if (state == Qt::ApplicationInactive) {
            m_inactiveStopTimer.start(3000);
            return;
        }
        // Hidden / Suspended
        m_inactiveStopTimer.stop();
        if (m_running) {
            m_resumeOnForeground = true;
            stopMesh();
        }
    });

    m_inactiveStopTimer.setSingleShot(true);
    connect(&m_inactiveStopTimer, &QTimer::timeout, this, [this]() {
        if (qApp->applicationState() != Qt::ApplicationActive && m_running) {
            m_resumeOnForeground = true;
            stopMesh();
        }
    });

    // graceful BLE shutdown on app termination — otherwise iOS keeps the BLE
    // registration and nags with "accessory would like to open" alerts
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        m_resumeOnForeground = false;
        stopMesh();
    });

    m_netClient = new MeshNetClient(this);
    m_netClient->setBaseUrl(m_settings->meshApiBase());
    m_netClient->setSignFunction([](const QString &canonical) -> QString {
#if defined(MESH_BRIDGE_AVAILABLE)
        return MeshBridgeWrapper::instance()->sign(canonical);
#else
        Q_UNUSED(canonical);
        return QString();
#endif
    });

    connect(m_netClient, &MeshNetClient::registered, this, [this](const QString &uin) {
        m_myUin = uin;
        m_settings->setMeshUin(uin);
        if (!m_pendingSubscription.isEmpty()) {
            m_settings->setMeshSubscriptionId(m_pendingSubscription);
        }
        applyBleNickname();
        emit runningChanged();
        if (!m_myName.isEmpty()) {
            m_netClient->setDisplayName(uin, m_myName);
        }
        m_netClient->startPolling(uin);
        refreshContactPresence();
    });
    connect(m_netClient, &MeshNetClient::registerFailed, this, [this](int status) {
        qWarning() << "[MESH] register failed, status:" << status;
        if (status == 409) {
            emit registerConflict();
        }
        if (!m_myUin.isEmpty()) {
            m_netClient->startPolling(m_myUin);
        }
    });
    connect(m_netClient, &MeshNetClient::pullFinished, this, [this](const QVariantList &messages) {
        for (const QVariant &variant : messages) {
            const QVariantMap message = variant.toMap();
            const QString ciphertext = message.value(QStringLiteral("ciphertext")).toString();
#if defined(MESH_BRIDGE_AVAILABLE)
            const QString opened = MeshBridgeWrapper::instance()->unseal(ciphertext);
            const QJsonObject object = QJsonDocument::fromJson(opened.toUtf8()).object();
            const QString fp = object.value(QStringLiteral("fp")).toString();
            QString text = object.value(QStringLiteral("text")).toString();
            // payload v1 is a JSON envelope carrying the sender's UIN; fall back
            // to raw text for envelopes sealed before it
            QString senderUin;
            const QJsonObject inner = QJsonDocument::fromJson(text.toUtf8()).object();
            if (inner.value(QStringLiteral("v")).toInt() == 1) {
                senderUin = inner.value(QStringLiteral("uin")).toString();
                text = inner.value(QStringLiteral("text")).toString();
            }
            if (!fp.isEmpty() && !text.isEmpty()) {
                handleIncomingPrivate(fp, text, senderUin, QStringLiteral("relay"));
            }
#else
            Q_UNUSED(ciphertext);
#endif
        }
        refreshContactPresence();
    });
    connect(m_netClient, &MeshNetClient::lookupFinished, this,
            [this](const QString &uin, bool found, bool online, const QString &serverName, const QStringList &devicePubkeys) {
                if (!found || devicePubkeys.isEmpty()) {
                    for (const QVariant &variant : m_contacts) {
                        const QVariantMap contact = variant.toMap();
                        if (contact.value(QStringLiteral("uin")).toString() == uin
                            && !contact.value(QStringLiteral("fp")).toString().isEmpty()) {
                            return;
                        }
                    }
                    if (m_netClient->isOnline()) {
                        emit contactNotFound(uin);
                    } else {
                        emit contactLookupFailed();
                    }
                    return;
                }
                const QString noisePubkeyHex = devicePubkeys.first();
                QString fp = fingerprintFromPubkey(noisePubkeyHex);
                QVariantList devices;
                for (const QString &pubkey : devicePubkeys) {
                    devices.append(pubkey);
                }
                for (int i = 0; i < m_contacts.size(); ++i) {
                    QVariantMap contact = m_contacts.at(i).toMap();
                    if (contact.value(QStringLiteral("uin")).toString() == uin) {
                        const QString existingFp = contact.value(QStringLiteral("fp")).toString();
                        if (!existingFp.isEmpty()) {
                            fp = existingFp;
                        }
                        contact[QStringLiteral("pubkey")] = noisePubkeyHex;
                        contact[QStringLiteral("devices")] = devices;
                        contact[QStringLiteral("serverName")] = serverName;
                        contact[QStringLiteral("fp")] = fp;
                        contact[QStringLiteral("online")] = online;
                        contact[QStringLiteral("reachable")] = m_reachable.contains(fp);
                        m_contacts[i] = contact;
                        saveContacts();
                        emit contactsChanged();
                        emit selectedContactChanged();
                        return;
                    }
                }
                QVariantMap contact;
                contact[QStringLiteral("uin")] = uin;
                contact[QStringLiteral("name")] = QString();
                contact[QStringLiteral("serverName")] = serverName;
                contact[QStringLiteral("pubkey")] = noisePubkeyHex;
                contact[QStringLiteral("devices")] = devices;
                contact[QStringLiteral("fp")] = fp;
                contact[QStringLiteral("reachable")] = m_reachable.contains(fp);
                contact[QStringLiteral("online")] = online;
                m_contacts.append(contact);
                saveContacts();
                emit contactsChanged();
                emit selectedContactChanged();
            });

    m_myUin = m_settings->meshUin();
    m_myName = m_settings->meshDisplayName();
    loadContacts();
    loadHistory();
}

MeshChatController::~MeshChatController()
{
    saveHistory();
}

bool MeshChatController::meshSupported() const
{
#if defined(MESH_BRIDGE_AVAILABLE)
    return true;
#else
    return false;
#endif
}

QString MeshChatController::myId() const
{
    return m_myUin;
}

void MeshChatController::setSelectedContact(const QString &id)
{
    const bool changed = m_selectedContact != id;
    m_selectedContact = id;
    if (changed) {
        applyFilter();
        emit selectedContactChanged();
    }
    if (!id.isEmpty() && !id.startsWith(QStringLiteral("fp:"))) {
        m_netClient->lookup(id);
    }
}

QString MeshChatController::selectedContactName() const
{
    for (const QVariant &variant : m_contacts) {
        const QVariantMap contact = variant.toMap();
        if (contact.value(QStringLiteral("uin")).toString() == m_selectedContact) {
            return contactLabel(contact);
        }
    }
    return m_selectedContact;
}

QString MeshChatController::selectedContactStatus() const
{
    for (const QVariant &variant : m_contacts) {
        const QVariantMap contact = variant.toMap();
        if (contact.value(QStringLiteral("uin")).toString() == m_selectedContact) {
            if (contact.value(QStringLiteral("reachable")).toBool()) {
                return QStringLiteral("ble");
            }
            if (contact.value(QStringLiteral("online")).toBool()) {
                return QStringLiteral("online");
            }
            return QStringLiteral("offline");
        }
    }
    return QStringLiteral("offline");
}

void MeshChatController::updateContactPreview(const QString &contactId, const QString &text, const QVariant &time)
{
    for (int i = 0; i < m_contacts.size(); ++i) {
        QVariantMap contact = m_contacts.at(i).toMap();
        if (contact.value(QStringLiteral("uin")).toString() == contactId) {
            contact[QStringLiteral("lastText")] = text;
            contact[QStringLiteral("lastTime")] = time;
            m_contacts[i] = contact;
            emit contactsChanged();
            return;
        }
    }
}

void MeshChatController::applyFilter()
{
    m_filteredMessages.clear();
    for (const QVariant &variant : m_allMessages) {
        const QVariantMap message = variant.toMap();
        if (m_selectedContact.isEmpty() || message.value(QStringLiteral("contact")).toString() == m_selectedContact) {
            m_filteredMessages.append(message);
        }
    }
    emit messagesChanged();
}

void MeshChatController::appendMessage(const QString &contactId, const QString &sender, const QString &text, bool isMine,
                                       bool delivered, const QString &transport)
{
    QVariantMap message;
    message[QStringLiteral("contact")] = contactId;
    message[QStringLiteral("sender")] = sender;
    message[QStringLiteral("text")] = text;
    message[QStringLiteral("ts")] = QDateTime::currentSecsSinceEpoch();
    message[QStringLiteral("time")] = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    message[QStringLiteral("isMine")] = isMine;
    message[QStringLiteral("delivered")] = delivered;
    message[QStringLiteral("transport")] = transport;
    m_allMessages.append(message);
    saveHistory();
    updateContactPreview(contactId, text, message.value(QStringLiteral("time")));

    if (m_selectedContact.isEmpty() || contactId == m_selectedContact) {
        m_filteredMessages.append(message);
        emit messagesChanged();
    }
}

void MeshChatController::loadContacts()
{
    m_contacts.clear();

    const QJsonArray saved = QJsonDocument::fromJson(m_settings->meshContacts().toUtf8()).array();
    for (const auto &value : saved) {
        const QJsonObject object = value.toObject();
        QVariantMap contact;
        contact[QStringLiteral("uin")] = object.value(QStringLiteral("uin")).toString();
        contact[QStringLiteral("name")] = object.value(QStringLiteral("name")).toString();
        contact[QStringLiteral("serverName")] = object.value(QStringLiteral("serverName")).toString();
        contact[QStringLiteral("pubkey")] = object.value(QStringLiteral("pubkey")).toString();
        contact[QStringLiteral("devices")] = object.value(QStringLiteral("devices")).toArray().toVariantList();
        contact[QStringLiteral("fp")] = object.value(QStringLiteral("fp")).toString();
        const QString savedUin = contact.value(QStringLiteral("uin")).toString();
        if (savedUin.isEmpty() || savedUin == QStringLiteral("broadcast")) {
            continue;
        }
        contact[QStringLiteral("reachable")] = m_reachable.contains(contact.value(QStringLiteral("fp")).toString());
        contact[QStringLiteral("online")] = false;
        m_contacts.append(contact);
    }
    emit contactsChanged();
}

void MeshChatController::saveContacts() const
{
    QJsonArray saved;
    for (const QVariant &variant : m_contacts) {
        const QVariantMap contact = variant.toMap();
        const QString uin = contact.value(QStringLiteral("uin")).toString();
        if (uin.startsWith(QStringLiteral("fp:"))) {
            continue;
        }
        QJsonObject object;
        object[QStringLiteral("uin")] = uin;
        object[QStringLiteral("name")] = contact.value(QStringLiteral("name")).toString();
        object[QStringLiteral("serverName")] = contact.value(QStringLiteral("serverName")).toString();
        object[QStringLiteral("pubkey")] = contact.value(QStringLiteral("pubkey")).toString();
        object[QStringLiteral("devices")] = QJsonArray::fromVariantList(contact.value(QStringLiteral("devices")).toList());
        object[QStringLiteral("fp")] = contact.value(QStringLiteral("fp")).toString();
        saved.append(object);
    }
    m_settings->setMeshContacts(QString::fromUtf8(QJsonDocument(saved).toJson(QJsonDocument::Compact)));
}

void MeshChatController::loadHistory()
{
    QFile file(historyFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    m_allMessages.clear();
    const QJsonArray saved = root.value(QStringLiteral("messages")).toArray();
    for (const auto &value : saved) {
        QVariantMap message = value.toObject().toVariantMap();
        // repair older entries saved without a sender label (rendered as "?")
        if (!message.value(QStringLiteral("isMine")).toBool()
            && message.value(QStringLiteral("sender")).toString().isEmpty()) {
            message[QStringLiteral("sender")] = message.value(QStringLiteral("contact")).toString();
        }
        m_allMessages.append(message);
    }
    // restore conversation list previews from history
    for (const QVariant &variant : m_allMessages) {
        const QVariantMap message = variant.toMap();
        const QString contactId = message.value(QStringLiteral("contact")).toString();
        updateContactPreview(contactId, message.value(QStringLiteral("text")).toString(),
                             message.value(QStringLiteral("time")));
    }
    applyFilter();
}

void MeshChatController::saveHistory() const
{
    // keep only the most recent messages per contact
    QMap<QString, int> seenPerContact;
    QJsonArray saved;
    for (int i = m_allMessages.size() - 1; i >= 0; --i) {
        const QVariantMap message = m_allMessages.at(i).toMap();
        const QString contactId = message.value(QStringLiteral("contact")).toString();
        if (seenPerContact.value(contactId) >= MAX_HISTORY_PER_CONTACT) {
            continue;
        }
        seenPerContact[contactId]++;
        saved.prepend(QJsonObject::fromVariantMap(message));
    }

    QJsonObject root;
    root[QStringLiteral("messages")] = saved;

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QFile file(historyFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();
    }
}

void MeshChatController::refreshContacts()
{
    QVariantList nearby;
#if defined(MESH_BRIDGE_AVAILABLE)
    m_reachable.clear();
    if (m_running) {
        const QJsonArray peers = QJsonDocument::fromJson(MeshBridgeWrapper::instance()->peersJson().toUtf8()).array();
        for (const auto &value : peers) {
            const QJsonObject object = value.toObject();
            const QString fp = object.value(QStringLiteral("fp")).toString();
            const QString nick = object.value(QStringLiteral("nick")).toString();
            if (fp.isEmpty()) {
                continue;
            }
            m_reachable.append(fp);
            if (nick.isEmpty() || nick == m_myUin) {
                continue;
            }
            // nearby section is only for new faces: skip peers already saved as contacts
            bool isContact = false;
            for (const QVariant &existing : m_contacts) {
                const QVariantMap contact = existing.toMap();
                if (contact.value(QStringLiteral("fp")).toString() == fp
                    || (!nick.isEmpty() && contact.value(QStringLiteral("uin")).toString() == nick)) {
                    isContact = true;
                    break;
                }
            }
            if (isContact) {
                continue;
            }
            QVariantMap peer;
            peer[QStringLiteral("fp")] = fp;
            peer[QStringLiteral("nick")] = nick;
            nearby.append(peer);
        }
    }
#endif
    const bool nearbyChanged = nearby != m_nearbyPeers;
    m_nearbyPeers = nearby;

    bool changed = nearbyChanged;
    for (int i = 0; i < m_contacts.size(); ++i) {
        QVariantMap contact = m_contacts.at(i).toMap();
        const bool reachable = m_reachable.contains(contact.value(QStringLiteral("fp")).toString());
        if (contact.value(QStringLiteral("reachable")).toBool() != reachable) {
            contact[QStringLiteral("reachable")] = reachable;
            m_contacts[i] = contact;
            changed = true;
        }
    }
    if (changed) {
        emit contactsChanged();
        emit selectedContactChanged(); // selected contact status may have flipped
    }
}

void MeshChatController::copyMyIdToClipboard()
{
    QGuiApplication::clipboard()->setText(myId());
}

void MeshChatController::addContact(const QString &uin, const QString &name)
{
    Q_UNUSED(name);
    const QString id = uin.trimmed();
    if (id.isEmpty()) {
        return;
    }
    for (const QVariant &variant : m_contacts) {
        if (variant.toMap().value(QStringLiteral("uin")).toString() == id) {
            return; // already there
        }
    }
    // resolve UIN → pubkey/fingerprint through the relay, the contact is
    // appended (or refreshed) in the lookupFinished handler
    m_netClient->lookup(id);
}

void MeshChatController::removeContact(const QString &uin)
{
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts.at(i).toMap().value(QStringLiteral("uin")).toString() == uin) {
            m_contacts.removeAt(i);
            break;
        }
    }
    saveContacts();
    if (m_selectedContact == uin) {
        setSelectedContact(QString());
    }
    emit contactsChanged();
}

void MeshChatController::addNearbyPeer(const QString &uin, const QString &fingerprint)
{
    const QString id = uin.trimmed();
    const QString fp = fingerprint.trimmed();
    if (id.isEmpty() || fp.isEmpty() || id == m_myUin) {
        return;
    }
    for (int i = 0; i < m_contacts.size(); ++i) {
        QVariantMap contact = m_contacts.at(i).toMap();
        if (contact.value(QStringLiteral("uin")).toString() != id) {
            continue;
        }
        if (contact.value(QStringLiteral("fp")).toString() != fp) {
            contact[QStringLiteral("fp")] = fp;
            contact[QStringLiteral("reachable")] = m_reachable.contains(fp);
            m_contacts[i] = contact;
            saveContacts();
            emit contactsChanged();
        }
        if (m_netClient->isOnline()) {
            m_netClient->lookup(id);
        }
        return;
    }
    QVariantMap contact;
    contact[QStringLiteral("uin")] = id;
    contact[QStringLiteral("name")] = id;
    contact[QStringLiteral("fp")] = fp;
    contact[QStringLiteral("reachable")] = true;
    contact[QStringLiteral("online")] = false;
    m_contacts.append(contact);
    saveContacts();
    emit contactsChanged();
    if (m_netClient->isOnline()) {
        m_netClient->lookup(id);
    }
}

void MeshChatController::handleIncomingPrivate(const QString &senderFingerprint, const QString &text,
                                               const QString &senderUin, const QString &transport)
{
    // sender named a UIN (sealed relay payload v1): merge into the UIN
    // conversation, upgrading the ephemeral fp-contact if there is one
    if (!senderUin.isEmpty()) {
        for (int i = 0; i < m_contacts.size(); ++i) {
            QVariantMap contact = m_contacts.at(i).toMap();
            const QString uin = contact.value(QStringLiteral("uin")).toString();
            if (uin == senderUin) {
                if (contact.value(QStringLiteral("fp")).toString() != senderFingerprint) {
                    contact[QStringLiteral("fp")] = senderFingerprint;
                    contact[QStringLiteral("reachable")] = m_reachable.contains(senderFingerprint);
                    m_contacts[i] = contact;
                    saveContacts();
                    emit contactsChanged();
                }
                appendMessage(senderUin, contactLabel(contact), text, false, true, transport);
                return;
            }
        }
        const QString ephemeralId = QStringLiteral("fp:") + senderFingerprint;
        for (int i = 0; i < m_contacts.size(); ++i) {
            QVariantMap contact = m_contacts.at(i).toMap();
            if (contact.value(QStringLiteral("uin")).toString() == ephemeralId) {
                // upgrade the ephemeral contact to the real UIN, re-keying its history
                contact[QStringLiteral("uin")] = senderUin;
                contact[QStringLiteral("name")] = senderUin;
                m_contacts[i] = contact;
                for (int j = 0; j < m_allMessages.size(); ++j) {
                    QVariantMap message = m_allMessages.at(j).toMap();
                    if (message.value(QStringLiteral("contact")).toString() == ephemeralId) {
                        message[QStringLiteral("contact")] = senderUin;
                        m_allMessages[j] = message;
                    }
                }
                if (m_selectedContact == ephemeralId) {
                    m_selectedContact = senderUin;
                    emit selectedContactChanged();
                }
                applyFilter();
                saveContacts();
                emit contactsChanged();
                appendMessage(senderUin, senderUin, text, false, true, transport);
                m_netClient->lookup(senderUin); // fetch pubkey/online so we can reply
                return;
            }
        }
        // brand new contact, known only by UIN so far — lookup completes it
        QVariantMap contact;
        contact[QStringLiteral("uin")] = senderUin;
        contact[QStringLiteral("name")] = senderUin;
        contact[QStringLiteral("fp")] = senderFingerprint;
        contact[QStringLiteral("reachable")] = m_reachable.contains(senderFingerprint);
        contact[QStringLiteral("online")] = true;
        m_contacts.append(contact);
        emit contactsChanged();
        appendMessage(senderUin, senderUin, text, false, true, transport);
        m_netClient->lookup(senderUin);
        return;
    }

    // map the sender's BLE identity to a saved contact; unknown senders get an
    // ephemeral contact (not persisted — it cannot be resolved to a UIN offline)
    for (const QVariant &variant : m_contacts) {
        const QVariantMap contact = variant.toMap();
        if (!contact.value(QStringLiteral("fp")).toString().isEmpty()
            && contact.value(QStringLiteral("fp")).toString() == senderFingerprint) {
            appendMessage(contact.value(QStringLiteral("uin")).toString(),
                          contactLabel(contact), text, false, true, transport);
            return;
        }
    }
    const QString ephemeralId = QStringLiteral("fp:") + senderFingerprint;
    bool known = false;
    for (const QVariant &variant : m_contacts) {
        if (variant.toMap().value(QStringLiteral("uin")).toString() == ephemeralId) {
            known = true;
            break;
        }
    }
    if (!known) {
        QVariantMap contact;
        contact[QStringLiteral("uin")] = ephemeralId;
        contact[QStringLiteral("name")] = senderFingerprint.left(8);
        contact[QStringLiteral("fp")] = senderFingerprint;
        contact[QStringLiteral("reachable")] = true;
        contact[QStringLiteral("online")] = false;
        m_contacts.append(contact);
        emit contactsChanged();
    }
    appendMessage(ephemeralId, senderFingerprint.left(8), text, false, true, transport);
}

void MeshChatController::sendViaRelay(const QVariantMap &contact, const QString &text)
{
    const QString uin = contact.value(QStringLiteral("uin")).toString();
    QStringList devices;
    for (const QVariant &device : contact.value(QStringLiteral("devices")).toList()) {
        const QString pubkey = device.toString();
        if (!pubkey.isEmpty()) {
            devices.append(pubkey);
        }
    }
    const QString pubkey = contact.value(QStringLiteral("pubkey")).toString();
    if (devices.isEmpty() && !pubkey.isEmpty()) {
        devices.append(pubkey);
    }

    bool delivered = false;
#if defined(MESH_BRIDGE_AVAILABLE)
    if (devices.isEmpty()) {
        m_netClient->lookup(uin);
    } else if (!m_myUin.isEmpty() && m_netClient->isOnline()) {
        QJsonObject payload;
        payload[QStringLiteral("v")] = 1;
        payload[QStringLiteral("uin")] = m_myUin;
        payload[QStringLiteral("text")] = text;
        const QString payloadJson = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        QJsonArray envelopes;
        for (const QString &device : devices) {
            const QString sealed = MeshBridgeWrapper::instance()->sealTo(device, payloadJson);
            if (sealed.isEmpty()) {
                continue;
            }
            QJsonObject envelope;
            envelope[QStringLiteral("device_pubkey")] = device;
            envelope[QStringLiteral("ciphertext")] = sealed;
            envelopes.append(envelope);
        }
        qWarning() << "[MESH] relay send to" << uin << "devices:" << envelopes.size();
        if (!envelopes.isEmpty()) {
            m_netClient->send(m_myUin, uin, envelopes);
            delivered = true;
        }
    } else {
        qWarning() << "[MESH] relay send skipped: devices empty:" << devices.isEmpty() << "myUin empty:"
                   << m_myUin.isEmpty() << "online:" << m_netClient->isOnline();
    }
#endif
    appendMessage(uin, tr("me"), text, true, delivered, QStringLiteral("relay"));
    if (!delivered) {
        emit peerUnreachable(uin);
    }
}

// MVP identity: iOS keeps Noise keys in the keychain across reinstall, so
// register returns the same UIN. Android EncryptedSharedPreferences do not,
// so a reinstall gets a new keypair and a new UIN.
void MeshChatController::applyBleNickname()
{
#if defined(MESH_BRIDGE_AVAILABLE)
    MeshBridgeWrapper::instance()->setNickname(m_myUin.isEmpty() ? QStringLiteral("anon") : m_myUin);
#endif
}

void MeshChatController::registerOnRelay()
{
#if defined(MESH_BRIDGE_AVAILABLE)
    MeshBridgeWrapper *bridge = MeshBridgeWrapper::instance();
    const QString pubkey = bridge->myPubkey();
    const QString signingPubkey = bridge->mySigningPubkey();
    if (pubkey.isEmpty() || signingPubkey.isEmpty()) {
        return;
    }
    const QString subscriptionId = m_subscriptionIdSource ? m_subscriptionIdSource() : QString();
    if (subscriptionId.isEmpty()) {
        qWarning() << "[MESH] no subscription id, relay register skipped";
        if (!m_myUin.isEmpty()) {
            m_netClient->startPolling(m_myUin);
        }
        return;
    }
    m_pendingSubscription = subscriptionId;
    m_netClient->registerDevice(subscriptionId, pubkey, signingPubkey, m_myName);
#endif
}

void MeshChatController::setMyName(const QString &name)
{
    QString trimmed;
    for (const QChar ch : name) {
        if (ch.category() == QChar::Other_Control) {
            continue;
        }
        trimmed.append(ch);
        if (trimmed.size() >= 32) {
            break;
        }
    }
    trimmed = trimmed.trimmed();
    if (m_myName == trimmed) {
        return;
    }
    m_myName = trimmed;
    m_settings->setMeshDisplayName(trimmed);
    emit myNameChanged();
    if (!m_myUin.isEmpty()) {
        m_netClient->setDisplayName(m_myUin, trimmed);
    }
}

void MeshChatController::setContactPetName(const QString &uin, const QString &name)
{
    const QString trimmed = name.trimmed();
    for (int i = 0; i < m_contacts.size(); ++i) {
        QVariantMap contact = m_contacts.at(i).toMap();
        if (contact.value(QStringLiteral("uin")).toString() != uin) {
            continue;
        }
        contact[QStringLiteral("name")] = trimmed;
        m_contacts[i] = contact;
        saveContacts();
        emit contactsChanged();
        emit selectedContactChanged();
        return;
    }
}

void MeshChatController::onSubscriptionChanged()
{
    if (m_running) {
        registerOnRelay();
    }
}

void MeshChatController::refreshContactPresence()
{
    for (const QVariant &variant : m_contacts) {
        const QVariantMap contact = variant.toMap();
        const QString uin = contact.value(QStringLiteral("uin")).toString();
        if (uin.isEmpty() || uin.startsWith(QStringLiteral("fp:"))) {
            continue;
        }
        if (contact.value(QStringLiteral("fp")).toString().isEmpty()
            && contact.value(QStringLiteral("pubkey")).toString().isEmpty()) {
            continue;
        }
        m_netClient->lookup(uin);
    }
}

void MeshChatController::startMesh()
{
#if defined(MESH_BRIDGE_AVAILABLE)
    if (m_running) {
        return;
    }
    MeshBridgeWrapper *bridge = MeshBridgeWrapper::instance();
    applyBleNickname();
    bridge->onMessage = [](const QString &, const QString &) {};
    bridge->onPrivateMessage = [this](const QString &senderFingerprint, const QString &content) {
        handleIncomingPrivate(senderFingerprint, content, QString(), QStringLiteral("ble"));
    };
    bridge->onPeerCountChanged = [this](int count) {
        m_peerCount = count;
        emit peerCountChanged();
        refreshContacts();
    };
    bridge->start();
    m_peerCount = bridge->peerCount();
    m_running = true;
    refreshContacts();
    registerOnRelay();
    refreshContactPresence();
    emit runningChanged();
    emit peerCountChanged();
#endif
}

void MeshChatController::stopMesh()
{
#if defined(MESH_BRIDGE_AVAILABLE)
    if (!m_running) {
        return;
    }
    m_netClient->stopPolling();
    MeshBridgeWrapper *bridge = MeshBridgeWrapper::instance();
    bridge->onMessage = nullptr;
    bridge->onPrivateMessage = nullptr;
    bridge->onPeerCountChanged = nullptr;
    bridge->stop();
    m_running = false;
    m_peerCount = 0;
    m_reachable.clear();
    saveHistory();
    refreshContacts();
    emit runningChanged();
    emit peerCountChanged();
#endif
}

void MeshChatController::sendMessage(const QString &text)
{
#if defined(MESH_BRIDGE_AVAILABLE)
    const QString trimmed = text.trimmed();
    if (!m_running || trimmed.isEmpty()) {
        return;
    }

    if (m_selectedContact.isEmpty()) {
        return;
    }

    QVariantMap contact;
    for (const QVariant &variant : m_contacts) {
        if (variant.toMap().value(QStringLiteral("uin")).toString() == m_selectedContact) {
            contact = variant.toMap();
            break;
        }
    }
    if (contact.isEmpty()) {
        return;
    }

    // Transport priority: BLE mesh first when the peer is in range (true
    // last-mile, no server involved), relay when the peer is only reachable
    // through the internet. If BLE send fails at the bridge, fall back to relay.
    const QString fp = contact.value(QStringLiteral("fp")).toString();
    const QString pubkey = contact.value(QStringLiteral("pubkey")).toString();
    const bool canRelay = !pubkey.isEmpty() && m_netClient->isOnline();
    const bool canBle = !fp.isEmpty() && m_reachable.contains(fp);
    if (canBle) {
        const bool sent = MeshBridgeWrapper::instance()->sendTo(fp, trimmed);
        qWarning() << "[MESH] send via BLE to" << fp.left(12) << "sendTo=" << sent;
        if (sent) {
            appendMessage(m_selectedContact, tr("me"), trimmed, true, true, QStringLiteral("ble"));
            return;
        }
        if (!canRelay) {
            appendMessage(m_selectedContact, tr("me"), trimmed, true, false, QStringLiteral("ble"));
            emit peerUnreachable(m_selectedContact);
            return;
        }
    }
    sendViaRelay(contact, trimmed);
#else
    Q_UNUSED(text);
#endif
}
