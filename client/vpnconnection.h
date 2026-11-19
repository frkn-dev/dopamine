#ifndef VPNCONNECTION_H
#define VPNCONNECTION_H

#include <QObject>
#include <QMetaObject>
#include <atomic>
#include <QSet>
#include <QString>
#include <QScopedPointer>
#include <QRemoteObjectNode>
#include <QTimer>

#include "protocols/vpnprotocol.h"
#include "core/defs.h"
#include "settings.h"

#ifdef AMNEZIA_DESKTOP
#include "core/ipcclient.h"
#endif

#ifdef Q_OS_ANDROID
#include "protocols/android_vpnprotocol.h"
#endif

using namespace amnezia;

class VpnConnection : public QObject
{
    Q_OBJECT

public:
    explicit VpnConnection(std::shared_ptr<Settings> settings, QObject* parent = nullptr);
    ~VpnConnection() override;

    static QString bytesPerSecToText(quint64 bytes);

    ErrorCode lastError() const;

    QSharedPointer<VpnProtocol> vpnProtocol() const;

    const QString &remoteAddress() const;
    void addSitesRoutes(const QString &gw, Settings::RouteMode mode);

#ifdef Q_OS_ANDROID
    void restoreConnection();
#endif

public slots:
    void connectToVpn(int serverIndex, const ServerCredentials &credentials, DockerContainer container, const QJsonObject &vpnConfiguration);
    void reconnectToVpn();
    void disconnectFromVpn();

    void onKillSwitchModeChanged(bool enabled);
    void disconnectSlots();

signals:
    void bytesChanged(quint64 receivedBytes, quint64 sentBytes);
    void connectionStateChanged(Vpn::ConnectionState state);
    void vpnProtocolError(amnezia::ErrorCode error);

    void serviceIsNotReady();

protected slots:
    void onBytesChanged(quint64 receivedBytes, quint64 sentBytes);
    void onConnectionStateChanged(Vpn::ConnectionState state);

    void setConnectionState(Vpn::ConnectionState state);

protected:
    QSharedPointer<VpnProtocol> m_vpnProtocol;

private:
    std::shared_ptr<Settings> m_settings;
    QJsonObject m_vpnConfiguration;
    QJsonObject m_routeMode;
    QString m_remoteAddress;

    // lastError() is called from the GUI thread: the owner thread publishes the value here
    // instead of letting other threads touch m_vpnProtocol
    std::atomic<int> m_lastErrorCache { static_cast<int>(ErrorCode::InternalError) };

    // Only for iOS for now, check counters
    QTimer m_checkTimer;

#ifdef Q_OS_ANDROID
   AndroidVpnProtocol* androidVpnProtocol = nullptr;

   AndroidVpnProtocol* createDefaultAndroidVpnProtocol();
   void createAndroidConnections();

   // A connect request sent while a tunnel or an attempt is still up makes the Android
   // service restart it by itself (DISCONNECTING -> DISCONNECTED -> CONNECTING). While this
   // is set, those teardown states of the previous tunnel are not forwarded.
   bool m_androidRestartPending = false;
#endif

   Vpn::ConnectionState m_connectionState = Vpn::ConnectionState::Disconnected;

   void createProtocolConnections();

   void appendSplitTunnelingConfig();
   void appendKillSwitchConfig();

#ifdef AMNEZIA_DESKTOP
   // Route-based site split tunneling resolves domains once at connect, but
   // CDN-fronted sites rotate IPs within minutes. While connected the active
   // list is re-resolved periodically and the route table patched with the
   // delta only (refreshSitesRoutes applies it).
   void refreshSitesRoutes();
   void applySplitRefreshDelta(const QSet<QString> &resolved);

   QTimer m_splitRefreshTimer;
   QString m_splitRefreshGw;
   Settings::RouteMode m_splitRefreshMode = Settings::VpnAllSites;
   QSet<QString> m_installedSplitRoutes;
   QSet<QString> m_splitRefreshResolved;
   quint64 m_splitRefreshGeneration = 0;
   int m_splitRefreshPending = 0;
   // bumped on every non-Connected state and per addSitesRoutes call; drops late DNS callbacks
   quint64 m_sitesRoutesGeneration = 0;
#endif
};

#endif // VPNCONNECTION_H
