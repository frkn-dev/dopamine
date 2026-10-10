#ifndef MESHCHATCONTROLLER_H
#define MESHCHATCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "settings.h"

class MeshNetClient;

// QML-facing controller for the mesh chat.
// Two transports, one UI: BLE mesh (BitChat core) for nearby peers, fcore
// relay (sealed Noise X envelopes) when there is internet but no peer in
// range. Contacts are ICQ-style numeric UINs issued by the relay; the
// fingerprint of the device Noise key stays an internal BLE detail.
// Real functionality only where a mesh bridge exists (iOS, Android); other
// platforms get a disabled stub so QML can bind unconditionally.
class MeshChatController : public QObject
{
    Q_OBJECT
public:
    explicit MeshChatController(const std::shared_ptr<Settings> &settings, QObject *parent = nullptr);
    ~MeshChatController() override;

    Q_PROPERTY(bool meshSupported READ meshSupported CONSTANT)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int peerCount READ peerCount NOTIFY peerCountChanged)
    Q_PROPERTY(QString myId READ myId NOTIFY runningChanged)
    Q_PROPERTY(QString myName READ myName NOTIFY myNameChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    Q_PROPERTY(QVariantList nearbyPeers READ nearbyPeers NOTIFY contactsChanged)
    Q_PROPERTY(QString selectedContact READ selectedContact WRITE setSelectedContact NOTIFY selectedContactChanged)
    Q_PROPERTY(QString selectedContactName READ selectedContactName NOTIFY selectedContactChanged)
    Q_PROPERTY(QString selectedContactStatus READ selectedContactStatus NOTIFY selectedContactChanged)

    bool meshSupported() const;
    bool isRunning() const { return m_running; }
    int peerCount() const { return m_peerCount; }
    QString myId() const;
    QString myName() const { return m_myName; }
    QVariantList messages() const { return m_filteredMessages; }
    QVariantList contacts() const { return m_contacts; }
    QVariantList nearbyPeers() const { return m_nearbyPeers; }
    QString selectedContact() const { return m_selectedContact; }
    void setSelectedContact(const QString &id);
    QString selectedContactName() const;
    QString selectedContactStatus() const;

    Q_INVOKABLE void refreshContacts();
    Q_INVOKABLE void copyMyIdToClipboard();
    Q_INVOKABLE void setMyName(const QString &name);
    Q_INVOKABLE void setContactPetName(const QString &uin, const QString &name);
    void setSubscriptionIdSource(std::function<QString()> source) { m_subscriptionIdSource = std::move(source); }

public slots:
    void startMesh();
    void stopMesh();
    void sendMessage(const QString &text);
    void addContact(const QString &uin, const QString &name);
    void addNearbyPeer(const QString &uin, const QString &fingerprint);
    void removeContact(const QString &uin);
    void onSubscriptionChanged();

signals:
    void runningChanged();
    void peerCountChanged();
    void messagesChanged();
    void contactsChanged();
    void selectedContactChanged();
    void peerUnreachable(const QString &uin);
    void contactNotFound(const QString &uin);
    void contactLookupFailed();
    void registerConflict();
    void myNameChanged();

private:
    void appendMessage(const QString &contactId, const QString &sender, const QString &text, bool isMine,
                       bool delivered = true, const QString &transport = QString());
    void updateContactPreview(const QString &contactId, const QString &text, const QVariant &time);
    void applyFilter();
    void loadContacts();
    void saveContacts() const;
    void loadHistory();
    void saveHistory() const;

    void handleIncomingPrivate(const QString &senderFingerprint, const QString &text, const QString &senderUin = QString(),
                               const QString &transport = QString());
    void sendViaRelay(const QVariantMap &contact, const QString &text);
    void registerOnRelay();
    void refreshContactPresence();
    void applyBleNickname();

    std::shared_ptr<Settings> m_settings;
    MeshNetClient *m_netClient = nullptr;

    bool m_running = false;
    bool m_resumeOnForeground = false; // mesh was up when the app backgrounded
    QTimer m_inactiveStopTimer; // iOS reports Inactive (not Suspended) on lock screen; stop mesh after a grace period
    int m_peerCount = 0;
    QString m_myUin;
    QString m_myName;
    QString m_pendingSubscription;
    QVariantList m_contacts; // [{uin, name, serverName, pubkey, devices, fp, reachable, online}]
    QVariantList m_nearbyPeers; // [{fp, nick}] from BLE announces, own UIN excluded
    QString m_selectedContact;
    QVariantList m_allMessages;    // every message, each with "contact" key
    QVariantList m_filteredMessages;

    QStringList m_reachable;       // identity fingerprints currently in BLE range

    std::function<QString()> m_subscriptionIdSource;
};

#endif // MESHCHATCONTROLLER_H
