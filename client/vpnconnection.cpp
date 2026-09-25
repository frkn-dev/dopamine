#include "vpnconnection.h"

#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPair>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <configurators/shadowsocks_configurator.h>
#include <configurators/wireguard_configurator.h>

#ifdef AMNEZIA_DESKTOP
    #include "core/ipcclient.h"
    #include <protocols/wireguardprotocol.h>
#endif

#ifdef Q_OS_ANDROID
    #include "platforms/android/android_controller.h"
    #include <QThread>

#endif

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    #include "platforms/ios/ios_controller.h"
#endif

#include "core/builtinSplitPresets.h"
#include "core/networkUtilities.h"
#include "vpnconnection.h"

#ifdef AMNEZIA_DESKTOP
// CDN-fronted split-tunnel sites rotate IPs within minutes; re-resolve the
// active list periodically and patch the route table with the delta.
constexpr int kSplitRefreshFirstDelayMs = 60 * 1000;
constexpr int kSplitRefreshIntervalMs = 5 * 60 * 1000;
#endif

VpnConnection::VpnConnection(std::shared_ptr<Settings> settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_checkTimer(new QTimer(this))
{
#ifdef AMNEZIA_DESKTOP
    m_splitRefreshTimer.setInterval(kSplitRefreshIntervalMs);
    connect(&m_splitRefreshTimer, &QTimer::timeout, this, &VpnConnection::refreshSitesRoutes);
#endif
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    m_checkTimer.setInterval(1000);
    connect(IosController::Instance(), &IosController::connectionStateChanged, this, &VpnConnection::setConnectionState);
    connect(IosController::Instance(), &IosController::bytesChanged, this, &VpnConnection::onBytesChanged);

    // iOS freezes timers while the app is suspended; if a transient state change
    // stopped the 1s NE status poll while backgrounded, it never restarts on its
    // own and the speed meter dies (arrows, no numbers) until a reconnect.
    // Self-heal on return to foreground.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive
            && (m_connectionState == Vpn::ConnectionState::Connected
                || m_connectionState == Vpn::ConnectionState::Reconnecting)) {
            IosController::Instance()->clearStatusRequest();
            if (!m_checkTimer.isActive()) {
                m_checkTimer.start();
            }
            QMetaObject::invokeMethod(IosController::Instance(), []() { IosController::Instance()->checkStatus(); },
                                      Qt::QueuedConnection);
        }
    });
#endif
}

VpnConnection::~VpnConnection()
{
}

void VpnConnection::onBytesChanged(quint64 receivedBytes, quint64 sentBytes)
{
    emit bytesChanged(receivedBytes, sentBytes);
}

void VpnConnection::onKillSwitchModeChanged(bool enabled)
{
#ifdef AMNEZIA_DESKTOP
    IpcClient::withInterface([enabled](QSharedPointer<IpcInterfaceReplica> iface){
        QRemoteObjectPendingReply<bool> reply = iface->refreshKillSwitch(enabled);
        if (reply.waitForFinished() && reply.returnValue())
            qDebug() << "VpnConnection::onKillSwitchModeChanged: Killswitch refreshed";
        else
            qWarning() << "VpnConnection::onKillSwitchModeChanged: Failed to execute remote refreshKillSwitch call";
    });
#endif
}

void VpnConnection::onConnectionStateChanged(Vpn::ConnectionState state)
{
#ifdef AMNEZIA_DESKTOP
    if (state != Vpn::ConnectionState::Connected) {
        m_splitRefreshTimer.stop();
        m_splitRefreshGeneration++;
        m_splitRefreshPending = 0;
        m_splitRefreshResolved.clear();
        m_installedSplitRoutes.clear();
    }

    auto container = m_settings->defaultContainer(m_settings->defaultServerIndex());

    IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        switch (state) {
            case Vpn::ConnectionState::Connected: {
                iface->resetIpStack();

                auto flushDns = iface->flushDns();
                if (flushDns.waitForFinished() && flushDns.returnValue())
                    qDebug() << "VpnConnection::onConnectionStateChanged: Successfully flushed DNS";
                else
                    qWarning() << "VpnConnection::onConnectionStateChanged: Failed to clear saved routes";


                if (!ContainerProps::isAwgContainer(container) &&
                    container != DockerContainer::WireGuard) {
                    QString dns1 = m_vpnConfiguration.value(config_key::dns1).toString();
                    QString dns2 = m_vpnConfiguration.value(config_key::dns2).toString();

                    // TODO: add error code handling for all routeAddList (or rework the code below)
                    iface->routeAddList(m_vpnProtocol->vpnGateway(), QStringList() << dns1 << dns2);

                    if (m_settings->isSitesSplitTunnelingEnabled()) {
                        iface->routeDeleteList(m_vpnProtocol->vpnGateway(), QStringList() << "0.0.0.0");
                        // qDebug() << "VpnConnection::onConnectionStateChanged :: adding custom routes, count:" << forwardIps.size();
                        if (m_settings->routeMode() == Settings::VpnOnlyForwardSites) {
                            QTimer::singleShot(1000, m_vpnProtocol.data(),
                                               [this]() { addSitesRoutes(m_vpnProtocol->vpnGateway(), m_settings->routeMode()); });
                        } else if (m_settings->routeMode() == Settings::VpnAllExceptSites) {
                            iface->routeAddList(m_vpnProtocol->vpnGateway(), QStringList() << "0.0.0.0/1");
                            iface->routeAddList(m_vpnProtocol->vpnGateway(), QStringList() << "128.0.0.0/1");

                            iface->routeAddList(m_vpnProtocol->routeGateway(), QStringList() << remoteAddress());
                            addSitesRoutes(m_vpnProtocol->routeGateway(), m_settings->routeMode());
                        }

                        if (m_settings->routeMode() == Settings::VpnOnlyForwardSites
                            || m_settings->routeMode() == Settings::VpnAllExceptSites) {
                            m_splitRefreshGeneration++;
                            QTimer::singleShot(kSplitRefreshFirstDelayMs, this,
                                               [this, generation = m_splitRefreshGeneration]() {
                                                   if (generation != m_splitRefreshGeneration
                                                       || m_connectionState != Vpn::ConnectionState::Connected)
                                                       return;
                                                   refreshSitesRoutes();
                                                   m_splitRefreshTimer.start();
                                               });
                        }
                    }
                }
            } break;
            case Vpn::ConnectionState::Disconnected:
            case Vpn::ConnectionState::Error: {
                auto flushDns = iface->flushDns();
                if (flushDns.waitForFinished() && flushDns.returnValue())
                    qDebug() << "VpnConnection::onConnectionStateChanged: Successfully flushed DNS";
                else
                    qWarning() << "VpnConnection::onConnectionStateChanged: Failed to flush DNS";

                auto clearSavedRoutes = iface->clearSavedRoutes();
                if (clearSavedRoutes.waitForFinished() && clearSavedRoutes.returnValue())
                    qDebug() << "VpnConnection::onConnectionStateChanged: Successfully cleared saved routes";
                else
                    qWarning() << "VpnConnection::onConnectionStateChanged: Failed to clear saved routes";
            } break;
            default:
                break;
        }
    });
#endif

#if defined(Q_OS_IOS) || defined(MACOS_NE)
    if (state == Vpn::ConnectionState::Connected ||
        state == Vpn::ConnectionState::Connecting ||
        state == Vpn::ConnectionState::Reconnecting) {
        m_checkTimer.start();
    } else {
        m_checkTimer.stop();
    }
#endif
}

const QString &VpnConnection::remoteAddress() const
{
    return m_remoteAddress;
}

void VpnConnection::addSitesRoutes(const QString &gw, Settings::RouteMode mode)
{
#ifdef AMNEZIA_DESKTOP
    m_splitRefreshGw = gw;
    m_splitRefreshMode = mode;

    QStringList ips;
    QStringList sites;
    const QVariantMap &m = m_settings->vpnSites(mode);
    for (auto i = m.constBegin(); i != m.constEnd(); ++i) {
        if (NetworkUtilities::checkIpSubnetFormat(i.key())) {
            ips.append(i.key());
        } else {
            if (NetworkUtilities::checkIpSubnetFormat(i.value().toString())) {
                ips.append(i.value().toString());
            }
            sites.append(i.key());
        }
    }
    ips.removeDuplicates();
    m_installedSplitRoutes = QSet<QString>(ips.begin(), ips.end());

    IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        iface->routeAddList(gw, ips);
    });

    // re-resolve domains
    for (const QString &site : sites) {
        const auto &cbResolv = [this, site, gw, mode, ips](const QHostInfo &hostInfo) {
            const QList<QHostAddress> &addresses = hostInfo.addresses();
            QString ipv4Addr;
            for (const QHostAddress &addr : hostInfo.addresses()) {
                if (addr.protocol() == QAbstractSocket::NetworkLayerProtocol::IPv4Protocol) {
                    const QString &ip = addr.toString();
                    // qDebug() << "VpnConnection::addSitesRoutes updating site" << site << ip;
                    if (!ips.contains(ip)) {
                        IpcClient::withInterface([&gw, &ip](QSharedPointer<IpcInterfaceReplica> iface) {
                            iface->routeAddList(gw, QStringList() << ip);
                        });
                        m_settings->addVpnSite(mode, site, ip);
                        m_installedSplitRoutes.insert(ip);
                    }
                    IpcClient::withInterface([](QSharedPointer<IpcInterfaceReplica> iface) {
                        auto reply = iface->flushDns();
                        if (reply.waitForFinished() || !reply.returnValue())
                            qWarning() << "VpnConnection::addSitesRoutes: Failed to flush DNS";
                    });
                    break;
                }
            }
        };
        QHostInfo::lookupHost(site, this, cbResolv);
    }
#endif
}

#ifdef AMNEZIA_DESKTOP
void VpnConnection::refreshSitesRoutes()
{
    if (m_connectionState != Vpn::ConnectionState::Connected || m_vpnProtocol.isNull())
        return;
    if (m_splitRefreshPending > 0) {
        qDebug() << "[SPLIT REFRESH] previous refresh still resolving, skipping cycle";
        return;
    }

    // Same parsing as addSitesRoutes for the keys (subnets are used as-is,
    // domains are re-resolved below). Stored values are NOT taken verbatim —
    // they are the previously resolved IPs and counting them in would make
    // removed IPs look still valid; they are only the fallback when a domain
    // fails to re-resolve.
    QStringList domains;
    const QVariantMap &sites = m_settings->vpnSites(m_splitRefreshMode);
    m_splitRefreshResolved.clear();
    for (auto i = sites.constBegin(); i != sites.constEnd(); ++i) {
        if (NetworkUtilities::checkIpSubnetFormat(i.key())) {
            m_splitRefreshResolved.insert(i.key());
        } else {
            domains.append(i.key());
        }
    }
    domains.removeDuplicates();

    if (domains.isEmpty()) {
        const QSet<QString> resolved = m_splitRefreshResolved;
        m_splitRefreshResolved.clear();
        applySplitRefreshDelta(resolved);
        return;
    }

    // QHostInfo::lookupHost resolves on Qt's internal worker threads and
    // delivers the callback on this thread — the UI never blocks on DNS.
    const quint64 generation = m_splitRefreshGeneration;
    m_splitRefreshPending = domains.size();
    for (const QString &domain : domains) {
        const QString lastKnownIp = sites.value(domain).toString();
        QHostInfo::lookupHost(domain, this, [this, generation, domain, lastKnownIp](const QHostInfo &hostInfo) {
            if (generation != m_splitRefreshGeneration || m_connectionState != Vpn::ConnectionState::Connected)
                return;

            QString ip;
            for (const QHostAddress &addr : hostInfo.addresses()) {
                if (addr.protocol() == QAbstractSocket::NetworkLayerProtocol::IPv4Protocol) {
                    ip = addr.toString();
                    break;
                }
            }
            if (!ip.isEmpty()) {
                m_splitRefreshResolved.insert(ip);
                m_settings->addVpnSite(m_splitRefreshMode, domain, ip);
            } else if (NetworkUtilities::checkIpSubnetFormat(lastKnownIp)) {
                // transient DNS failure: keep the last known route rather
                // than tearing it down
                m_splitRefreshResolved.insert(lastKnownIp);
            }

            if (--m_splitRefreshPending == 0) {
                const QSet<QString> resolved = m_splitRefreshResolved;
                m_splitRefreshResolved.clear();
                applySplitRefreshDelta(resolved);
            }
        });
    }
}

void VpnConnection::applySplitRefreshDelta(const QSet<QString> &resolved)
{
    if (m_connectionState != Vpn::ConnectionState::Connected)
        return;

    QStringList added;
    for (const QString &ip : resolved) {
        if (!m_installedSplitRoutes.contains(ip))
            added.append(ip);
    }
    QStringList removed;
    for (const QString &ip : m_installedSplitRoutes) {
        if (!resolved.contains(ip))
            removed.append(ip);
    }

    qDebug() << "[SPLIT REFRESH] added" << added.size() << ", removed" << removed.size();

    m_installedSplitRoutes = resolved;

    if (added.isEmpty() && removed.isEmpty())
        return;

    const QString gw = m_splitRefreshGw;
    IpcClient::withInterface([&](QSharedPointer<IpcInterfaceReplica> iface) {
        if (!added.isEmpty())
            iface->routeAddList(gw, added);
        if (!removed.isEmpty())
            iface->routeDeleteList(gw, removed);
    });
}
#endif

QSharedPointer<VpnProtocol> VpnConnection::vpnProtocol() const
{
    return m_vpnProtocol;
}

void VpnConnection::disconnectSlots()
{
    if (m_vpnProtocol) {
        m_vpnProtocol->disconnect();
    }
}

ErrorCode VpnConnection::lastError() const
{
#ifdef Q_OS_ANDROID
    return ErrorCode::AndroidError;
#endif

    if (m_vpnProtocol.isNull()) {
        return ErrorCode::InternalError;
    }

    return m_vpnProtocol.data()->lastError();
}

void VpnConnection::connectToVpn(int serverIndex, const ServerCredentials &credentials, DockerContainer container,
                                 const QJsonObject &vpnConfiguration)
{
    qDebug() << QString("Trying to connect to VPN, server index is %1, container is %2, route mode is")
                        .arg(serverIndex)
                        .arg(ContainerProps::containerToString(container))
             << m_settings->routeMode();

    m_remoteAddress = NetworkUtilities::getIPAddress(credentials.hostName);
    setConnectionState(Vpn::ConnectionState::Connecting);

    m_vpnConfiguration = vpnConfiguration;
    // Keep the server entry index in the config so platform controllers (e.g. iOS)
    // can build a unique tunnel identity even when several server entries share the
    // same hostName/description (otherwise their VPN profiles collide into one).
    m_vpnConfiguration[config_key::serverIndex] = serverIndex;

#ifdef AMNEZIA_DESKTOP
    if (m_vpnProtocol) {
        disconnect(m_vpnProtocol.data(), &VpnProtocol::protocolError, this, &VpnConnection::vpnProtocolError);
        m_vpnProtocol->stop();
        m_vpnProtocol.reset();
    }
    appendKillSwitchConfig();
#endif

    appendSplitTunnelingConfig();

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
    m_vpnProtocol.reset(VpnProtocol::factory(container, m_vpnConfiguration));
    if (!m_vpnProtocol) {
        setConnectionState(Vpn::ConnectionState::Error);
        return;
    }
    m_vpnProtocol->prepare();
#elif defined Q_OS_ANDROID
    androidVpnProtocol = createDefaultAndroidVpnProtocol();
    createAndroidConnections();

    m_vpnProtocol.reset(androidVpnProtocol);
#elif defined Q_OS_IOS || defined(MACOS_NE)
    Proto proto = ContainerProps::defaultProtocol(container);
    IosController::Instance()->connectVpn(proto, m_vpnConfiguration);
    // UniqueConnection avoids stacking another duplicate subscription on every retry
    connect(&m_checkTimer, &QTimer::timeout, IosController::Instance(), &IosController::checkStatus,
            Qt::UniqueConnection);
    return;
#endif

    createProtocolConnections();

    if (ErrorCode err = m_vpnProtocol->start(); err != ErrorCode::NoError) {
        setConnectionState(Vpn::ConnectionState::Error);
        emit vpnProtocolError(err);
    }
}

void VpnConnection::createProtocolConnections()
{
    connect(m_vpnProtocol.data(), &VpnProtocol::protocolError, this, &VpnConnection::vpnProtocolError);
    connect(m_vpnProtocol.data(), &VpnProtocol::connectionStateChanged, this, &VpnConnection::setConnectionState);
    connect(m_vpnProtocol.data(), SIGNAL(bytesChanged(quint64, quint64)), this, SLOT(onBytesChanged(quint64, quint64)));

#ifdef AMNEZIA_DESKTOP
    // UniqueConnection: this runs on every connect; without it each connect
    // would stack another duplicate reconnectToVpn subscription
    IpcClient::withInterface([this](QSharedPointer<IpcInterfaceReplica> rep) {
        QObject::connect(rep.data(), &IpcInterfaceReplica::networkChanged, this, &VpnConnection::reconnectToVpn,
                         Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
        QObject::connect(rep.data(), &IpcInterfaceReplica::wakeup, this, &VpnConnection::reconnectToVpn,
                         Qt::ConnectionType(Qt::QueuedConnection | Qt::UniqueConnection));
    });
#endif
}

void VpnConnection::appendKillSwitchConfig()
{
    m_vpnConfiguration.insert(config_key::killSwitchOption, QVariant(m_settings->isKillSwitchEnabled()).toString());
    m_vpnConfiguration.insert(config_key::routeLanThroughVpn, QVariant(m_settings->isRouteLanThroughVpn()).toString());
    m_vpnConfiguration.insert(config_key::allowedDnsServers, QVariant(m_settings->allowedDnsServers()).toJsonValue());
}

void VpnConnection::appendSplitTunnelingConfig()
{
    bool allowSiteBasedSplitTunneling = true;

    // this block is for old native configs and for old self-hosted configs
    auto protocolName = m_vpnConfiguration.value(config_key::vpnproto).toString();
    if (protocolName == ProtocolProps::protoToString(Proto::Awg) || protocolName == ProtocolProps::protoToString(Proto::WireGuard)) {
        allowSiteBasedSplitTunneling = false;
        auto configData = m_vpnConfiguration.value(protocolName + "_config_data").toObject();
        if (configData.value(config_key::allowed_ips).isString()) {
            QJsonArray allowedIpsJsonArray = QJsonArray::fromStringList(configData.value(config_key::allowed_ips).toString().split(", "));
            configData.insert(config_key::allowed_ips, allowedIpsJsonArray);
            m_vpnConfiguration.insert(protocolName + "_config_data", configData);
        } else if (configData.value(config_key::allowed_ips).isUndefined()) {
            auto nativeConfig = configData.value(config_key::config).toString();
            auto nativeConfigLines = nativeConfig.split("\n");
            for (auto &line : nativeConfigLines) {
                if (line.contains("AllowedIPs")) {
                    auto allowedIpsString = line.split(" = ");
                    if (allowedIpsString.size() < 1) {
                        break;
                    }
                    QJsonArray allowedIpsJsonArray = QJsonArray::fromStringList(allowedIpsString.at(1).split(", "));
                    configData.insert(config_key::allowed_ips, allowedIpsJsonArray);
                    m_vpnConfiguration.insert(protocolName + "_config_data", configData);
                    break;
                }
            }
        }

        if (configData.value(config_key::persistent_keep_alive).isUndefined()) {
            auto nativeConfig = configData.value(config_key::config).toString();
            auto nativeConfigLines = nativeConfig.split("\n");
            for (auto &line : nativeConfigLines) {
                if (line.contains("PersistentKeepalive")) {
                    auto persistentKeepaliveString = line.split(" = ");
                    if (persistentKeepaliveString.size() < 1) {
                        break;
                    }
                    configData.insert(config_key::persistent_keep_alive, persistentKeepaliveString.at(1));
                    m_vpnConfiguration.insert(protocolName + "_config_data", configData);
                    break;
                }
            }
        }

        QJsonArray allowedIpsJsonArray = configData.value(config_key::allowed_ips).toArray();
        // full tunnel = covers all IPv4; IPv6 (::/0) is not required — our API
        // configs are IPv4-only by design (see VpnConfigurationsController)
        if (allowedIpsJsonArray.contains("0.0.0.0/0")) {
            allowSiteBasedSplitTunneling = true;
        }
    }

    Settings::RouteMode routeMode = Settings::RouteMode::VpnAllSites;
    QJsonArray sitesJsonArray;
    if (m_settings->isSitesSplitTunnelingEnabled()) {
        routeMode = m_settings->routeMode();

        if (allowSiteBasedSplitTunneling) {
            auto sites = m_settings->getVpnIps(routeMode);
            for (const auto &site : sites) {
                sitesJsonArray.append(site);
            }

            if (sitesJsonArray.isEmpty()) {
                routeMode = Settings::RouteMode::VpnAllSites;
            } else if (routeMode == Settings::VpnOnlyForwardSites) {
                // Allow traffic to Amnezia DNS
                sitesJsonArray.append(m_vpnConfiguration.value(config_key::dns1).toString());
                sitesJsonArray.append(m_vpnConfiguration.value(config_key::dns2).toString());
            }
        }
    }

    m_vpnConfiguration.insert(config_key::splitTunnelType, routeMode);
    m_vpnConfiguration.insert(config_key::splitTunnelSites, sitesJsonArray);

    // 2x2 split tunneling: the manual site list (direction = routeMode) and the
    // service presets are flattened into include/exclude sets. A checked preset is
    // always the EXCEPTION to the default: it goes direct when the default route is
    // VPN (routeMode != VpnOnlyForwardSites) and via VPN when the default is direct.
    // The iOS network extension applies both sets simultaneously (excluded wins).
    QJsonArray includeSitesJsonArray;
    QJsonArray excludeSitesJsonArray;
    if (m_settings->isSitesSplitTunnelingEnabled() && allowSiteBasedSplitTunneling) {
        if (routeMode == Settings::VpnOnlyForwardSites) {
            includeSitesJsonArray = sitesJsonArray;
        } else if (routeMode == Settings::VpnAllExceptSites) {
            excludeSitesJsonArray = sitesJsonArray;
        }

        const QStringList enabledPresets = m_settings->splitPresetsEnabled();
        if (!enabledPresets.isEmpty()) {
            struct PresetBucket
            {
                QStringList cidrs;
                QStringList hosts;
            };
            PresetBucket followMode;
            PresetBucket alwaysDirect;
            PresetBucket alwaysVpn;

            QJsonArray presets = QJsonDocument::fromJson(m_settings->splitPresetsCache().toUtf8()).array();
            const QJsonArray builtinPresets = BuiltinSplitPresets::presets();
            for (const auto &value : builtinPresets) {
                presets.append(value);
            }
            for (const auto &value : presets) {
                const QJsonObject preset = value.toObject();
                const QString id = preset.value("id").toString();
                if (!enabledPresets.contains(id)) {
                    continue;
                }
                PresetBucket *bucket = &followMode;
                if (id == QLatin1String("builtin-ru-direct")
                    || id == QLatin1String("builtin-ru-banking")) {
                    bucket = &alwaysDirect;
                } else if (id == QLatin1String("builtin-ru-vpn")) {
                    bucket = &alwaysVpn;
                }
                const QJsonArray domains = preset.value("domains").toArray();
                for (const auto &domain : domains) {
                    const QString entry = domain.toString();
                    if (NetworkUtilities::checkIpSubnetFormat(entry)) {
                        bucket->cidrs.append(entry);
                    } else if (!entry.isEmpty()) {
                        bucket->hosts.append(entry);
                    }
                }
            }

            auto parseSubnet = [](const QString &cidr) -> QPair<QHostAddress, int> {
                const QStringList parts = cidr.split('/');
                QHostAddress addr(parts.value(0));
                int prefix = -1;
                if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                    prefix = parts.size() > 1 ? parts.at(1).toInt() : 32;
                }
                return { addr, prefix };
            };

            auto flattenBucket = [&](const PresetBucket &bucket) -> QJsonArray {
                QJsonArray out;
                QList<QPair<QHostAddress, int>> nets;
                for (const QString &cidr : bucket.cidrs) {
                    out.append(cidr);
                    const auto net = parseSubnet(cidr);
                    if (net.second >= 0) {
                        nets.append(net);
                    }
                }

                QStringList domainsToResolve = bucket.hosts;
                if (!domainsToResolve.isEmpty()) {
                    QVector<QHostInfo> resolvedInfos(domainsToResolve.size());
                    QEventLoop loop;
                    int remaining = domainsToResolve.size();
                    const int resolveTimeoutMs =
                            qMin(12000, 2500 + domainsToResolve.size() * 12);
                    QTimer::singleShot(resolveTimeoutMs, &loop, &QEventLoop::quit);
                    for (int i = 0; i < domainsToResolve.size(); ++i) {
                        QHostInfo::lookupHost(domainsToResolve.at(i), &loop,
                                [&resolvedInfos, &remaining, &loop, i](const QHostInfo &info) {
                                    resolvedInfos[i] = info;
                                    if (--remaining == 0) {
                                        loop.quit();
                                    }
                                });
                    }
                    loop.exec();
                    for (const QHostInfo &hostInfo : resolvedInfos) {
                        for (const auto &addr : hostInfo.addresses()) {
                            if (addr.protocol() != QAbstractSocket::IPv4Protocol) {
                                continue;
                            }
                            bool covered = false;
                            for (const auto &net : nets) {
                                if (addr.isInSubnet(net.first, net.second)) {
                                    covered = true;
                                    break;
                                }
                            }
                            if (!covered) {
                                out.append(addr.toString());
                            }
                            break;
                        }
                    }
                }
                return out;
            };

            const QJsonArray followIps = flattenBucket(followMode);
            const QJsonArray directIps = flattenBucket(alwaysDirect);
            const QJsonArray vpnIps = flattenBucket(alwaysVpn);

            auto appendAll = [](QJsonArray &dst, const QJsonArray &src) {
                for (const auto &v : src) {
                    dst.append(v);
                }
            };

            if (routeMode == Settings::VpnOnlyForwardSites) {
                appendAll(includeSitesJsonArray, followIps);
                appendAll(includeSitesJsonArray, vpnIps);
            } else {
                appendAll(excludeSitesJsonArray, followIps);
                appendAll(excludeSitesJsonArray, directIps);
            }

            const QVector<QPair<QHostAddress, int>> sharedCdn = {
                { QHostAddress(QStringLiteral("104.16.0.0")), 12 },
                { QHostAddress(QStringLiteral("104.64.0.0")), 10 },
                { QHostAddress(QStringLiteral("162.158.0.0")), 15 },
                { QHostAddress(QStringLiteral("172.64.0.0")), 13 },
                { QHostAddress(QStringLiteral("173.245.48.0")), 20 },
                { QHostAddress(QStringLiteral("188.114.96.0")), 20 },
                { QHostAddress(QStringLiteral("190.93.240.0")), 20 },
                { QHostAddress(QStringLiteral("197.234.240.0")), 22 },
                { QHostAddress(QStringLiteral("198.41.128.0")), 17 },
                { QHostAddress(QStringLiteral("141.101.64.0")), 18 },
                { QHostAddress(QStringLiteral("103.21.244.0")), 22 },
                { QHostAddress(QStringLiteral("103.22.200.0")), 22 },
                { QHostAddress(QStringLiteral("103.31.4.0")), 22 },
                { QHostAddress(QStringLiteral("151.101.0.0")), 16 },
                { QHostAddress(QStringLiteral("199.232.0.0")), 16 },
                { QHostAddress(QStringLiteral("23.32.0.0")), 11 },
                { QHostAddress(QStringLiteral("23.192.0.0")), 11 },
                { QHostAddress(QStringLiteral("2.16.0.0")), 13 },
                { QHostAddress(QStringLiteral("13.32.0.0")), 12 },
                { QHostAddress(QStringLiteral("13.224.0.0")), 12 },
                { QHostAddress(QStringLiteral("99.84.0.0")), 16 },
                { QHostAddress(QStringLiteral("76.223.0.0")), 16 },
                { QHostAddress(QStringLiteral("13.248.0.0")), 14 },
            };
            auto isSharedCdnHost = [&sharedCdn](const QString &entry) -> bool {
                if (entry.contains(QLatin1Char('/'))) {
                    return false;
                }
                const QHostAddress addr(entry);
                if (addr.protocol() != QAbstractSocket::IPv4Protocol) {
                    return false;
                }
                for (const auto &net : sharedCdn) {
                    if (addr.isInSubnet(net.first, net.second)) {
                        return true;
                    }
                }
                return false;
            };
            QJsonArray filteredExclude;
            for (const auto &value : excludeSitesJsonArray) {
                const QString entry = value.toString();
                if (!isSharedCdnHost(entry)) {
                    filteredExclude.append(entry);
                }
            }
            excludeSitesJsonArray = filteredExclude;
        }
    }
    m_vpnConfiguration.insert(config_key::splitTunnelIncludeSites, includeSitesJsonArray);
    m_vpnConfiguration.insert(config_key::splitTunnelExcludeSites, excludeSitesJsonArray);

    Settings::AppsRouteMode appsRouteMode = Settings::AppsRouteMode::VpnAllApps;
    QJsonArray appsJsonArray;
    if (m_settings->isAppsSplitTunnelingEnabled()) {
        appsRouteMode = m_settings->getAppsRouteMode();

        auto apps = m_settings->getVpnApps(appsRouteMode);
        for (const auto &app : apps) {
            appsJsonArray.append(app.appPath.isEmpty() ? app.packageName : app.appPath);
        }

        if (appsJsonArray.isEmpty()) {
            appsRouteMode = Settings::AppsRouteMode::VpnAllApps;
        }
    }

    m_vpnConfiguration.insert(config_key::appSplitTunnelType, appsRouteMode);
    m_vpnConfiguration.insert(config_key::splitTunnelApps, appsJsonArray);

    qDebug() << QString("Site split tunneling is %1, route mode is %2")
                        .arg(m_settings->isSitesSplitTunnelingEnabled() ? "enabled" : "disabled")
                        .arg(routeMode);
    qDebug() << QString("App split tunneling is %1, route mode is %2")
                        .arg(m_settings->isAppsSplitTunnelingEnabled() ? "enabled" : "disabled")
                        .arg(appsRouteMode);
}

#ifdef Q_OS_ANDROID
void VpnConnection::restoreConnection()
{
    createAndroidConnections();

    m_vpnProtocol.reset(androidVpnProtocol);

    createProtocolConnections();
}

void VpnConnection::createAndroidConnections()
{
    androidVpnProtocol = createDefaultAndroidVpnProtocol();

    connect(AndroidController::instance(), &AndroidController::connectionStateChanged, androidVpnProtocol,
            &AndroidVpnProtocol::setConnectionState);
    connect(AndroidController::instance(), &AndroidController::statisticsUpdated, androidVpnProtocol, &AndroidVpnProtocol::setBytesChanged);
}

AndroidVpnProtocol *VpnConnection::createDefaultAndroidVpnProtocol()
{
    return new AndroidVpnProtocol(m_vpnConfiguration);
}
#endif

QString VpnConnection::bytesPerSecToText(quint64 bytes)
{
    double mbps = bytes * 8 / 1e6;
    return QString("%1 %2").arg(QString::number(mbps, 'f', 2)).arg(tr("Mbps")); // Mbit/s
}

void VpnConnection::reconnectToVpn() {
    if (m_vpnProtocol.isNull())
        return;

    if (m_connectionState != Vpn::ConnectionState::Connected) {
        qWarning() << QString("Reconnect triggered on %1 during inappropriate state: %2; ignoring slot")
                              .arg(QMetaEnum::fromType<Vpn::ConnectionState>().valueToKey(m_connectionState));
        return;
    }

    qDebug() << "Reconnect triggered. Reconnecting to the server";

    setConnectionState(Vpn::ConnectionState::Reconnecting);

    m_vpnProtocol->stop();
    if (ErrorCode err = m_vpnProtocol->start(); err != ErrorCode::NoError) {
        setConnectionState(Vpn::ConnectionState::Error);
        emit vpnProtocolError(err);
    }
}

void VpnConnection::disconnectFromVpn()
{
#if defined(Q_OS_IOS) || defined(MACOS_NE)
    // iOS/macOS NE use IosController directly; m_vpnProtocol is not set there.
    IosController::Instance()->disconnectVpn();
    disconnect(&m_checkTimer, &QTimer::timeout, IosController::Instance(), &IosController::checkStatus);
#endif

    if (m_vpnProtocol.isNull()) {
        setConnectionState(Vpn::ConnectionState::Disconnected);
        return;
    }

    setConnectionState(Vpn::ConnectionState::Disconnecting);

#ifdef Q_OS_ANDROID
    auto *const connection = new QMetaObject::Connection;
    *connection = connect(AndroidController::instance(), &AndroidController::vpnStateChanged, this,
                          [this, connection](AndroidController::ConnectionState state) {
                              if (state == AndroidController::ConnectionState::DISCONNECTED) {
                                  setConnectionState(Vpn::ConnectionState::Disconnected);
                                  disconnect(*connection);
                                  delete connection;
                              }
                          });
#endif

    m_vpnProtocol->stop();

#if !defined(Q_OS_ANDROID) && !defined(AMNEZIA_DESKTOP)
    m_vpnProtocol->deleteLater();
#endif

    m_vpnProtocol = nullptr;
}

void VpnConnection::setConnectionState(Vpn::ConnectionState state) {
    onConnectionStateChanged(state);

    if (state == Vpn::Disconnected && m_connectionState == Vpn::Reconnecting)
        return;

    m_connectionState = state;
    emit connectionStateChanged(state);
}
